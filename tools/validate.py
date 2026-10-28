"""Replay full-precision ALNSAuditRunner CSVs against raw Solomon input.

Independent of Route/evaluator and of the validity flags exported by the solver.
No repair, rounding adjustment, rescheduling, or optional optimization is used.
"""
import csv
import hashlib
import json
import math
import sys
import argparse
from collections import Counter, defaultdict
from pathlib import Path


def read_instance(instance):
    nodes, params = [], {}
    for line in instance.read_text(encoding="utf-8-sig").splitlines()[1:]:
        if not line.strip():
            continue
        if "/" in line:
            key = line.strip()[0]
            if key not in "QCrgv" or key in params:
                raise ValueError("Unknown or duplicate vehicle parameter")
            params[key] = float(line.split("/")[1])
        else:
            f = line.split()
            if len(f) == 8 and f[1] in ("d", "f", "c"):
                nodes.append(dict(name=f[0], kind=f[1], x=float(f[2]), y=float(f[3]),
                                  demand=float(f[4]), ready=float(f[5]), due=float(f[6]),
                                  service=float(f[7])))
            else:
                raise ValueError("Malformed Solomon EVRPTW node: "+line)
    if not nodes or nodes[0]["kind"]!="d" or sum(node["kind"]=="d" for node in nodes)!=1:
        raise ValueError("Expected Solomon EVRPTW input with one depot first; JD is not supported")
    if not any(node["kind"]=="c" for node in nodes):
        raise ValueError("Instance contains no customers")
    if not {"Q","C","r","g","v"} <= params.keys():
        raise ValueError("Missing EVRPTW parameters Q, C, r, g, v; plain Solomon VRPTW is not EVRPTW")
    if any(not math.isfinite(value) or value<=0 for value in params.values()):
        raise ValueError("Vehicle/charging parameters must be finite and positive")
    for node in nodes:
        if not all(math.isfinite(node[key]) for key in ("x","y","demand","ready","due","service")):
            raise ValueError("Non-finite node data")
        if node["demand"]<0 or node["service"]<0 or node["ready"]>node["due"]:
            raise ValueError("Invalid demand/service/time window")
        if not node["name"].startswith({"d":"D","f":"S","c":"C"}[node["kind"]]):
            raise ValueError("Unsupported node name: "+node["name"])
    return nodes, params


def validate(instance, output):
    nodes, params = read_instance(instance)
    schedules = defaultdict(lambda: defaultdict(list))
    with (output/"schedules.csv").open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            schedules[int(row["solution"])][int(row["route"])].append(row)
    reports = []
    seen_results = set()
    with (output/"results.csv").open(newline="", encoding="utf-8") as handle:
        result_rows=list(csv.DictReader(handle))
    for result in result_rows:
        sid = int(result["solution"])
        if sid in seen_results:
            raise ValueError("Duplicate solution ID in results.csv")
        seen_results.add(sid)
        errors, coverage, actives, durations = [], Counter(), [], []
        total_distance = 0.0

        def require(ok, description):
            if not ok:
                errors.append(description)

        def near(a, b, description):
            require(math.isfinite(a) and math.isfinite(b) and
                    abs(a-b) <= 1e-7*max(1.0, abs(a), abs(b)),
                    f"{description}: actual={a}, expected={b}")

        require(sid in schedules, "missing solution schedule")
        for rid, rows in schedules[sid].items():
            context = f"route {rid}"
            positions = [int(r["position"]) for r in rows]
            sequence = [int(r["node"]) for r in rows]
            require(positions == list(range(len(rows))), context+": positions")
            require(len(rows) >= 2 and sequence[0] == sequence[-1] == 0, context+": depot endpoints")
            require(all(0 <= i < len(nodes) for i in sequence), context+": unknown node")
            if not all(0 <= i < len(nodes) for i in sequence):
                continue
            require(any(nodes[i]["kind"] == "c" for i in sequence), context+": used route has no customer")
            require(0 not in sequence[1:-1], context+": internal depot")
            clock, battery, load = nodes[0]["ready"], params["Q"], params["C"]
            wait_sum = 0.0
            for pos, row in enumerate(rows):
                node = nodes[sequence[pos]]
                context = f"route {rid} position {pos} node {node['name']}"
                if pos:
                    prev = nodes[sequence[pos-1]]
                    edge = math.hypot(node["x"]-prev["x"], node["y"]-prev["y"])
                    total_distance += edge
                    clock += edge/params["v"]
                    battery -= edge*params["r"]
                near(float(row["arrival"]), clock, context+": arrival")
                require(battery >= -1e-7, context+": negative arrival battery")
                wait = max(0.0, node["ready"]-clock) if node["kind"] != "d" else 0.0
                near(float(row["wait"]), wait, context+": wait")
                clock += wait
                wait_sum += wait
                require(clock <= node["due"]+1e-7, context+": time window")
                charge = float(row["charge"])
                require(charge >= -1e-7, context+": negative charge")
                if node["kind"] == "f":
                    battery += charge
                    clock += charge*params["g"]
                    require(clock <= node["due"]+1e-7, context+": station closing")
                else:
                    near(charge, 0.0, context+": charge outside station")
                    if node["kind"] == "c":
                        coverage[sequence[pos]] += 1
                        load -= node["demand"]
                        clock += node["service"]
                require(load >= -1e-7, context+": delivery capacity")
                require(battery <= params["Q"]+1e-7, context+": battery capacity")
                near(float(row["departure"]), clock, context+": departure")
                near(float(row["battery"]), battery, context+": battery conservation")
                near(float(row["load"]), load, context+": remaining load")
            duration = clock-nodes[0]["ready"]
            durations.append(duration)
            actives.append(duration-wait_sum)
        for nid, node in enumerate(nodes):
            if node["kind"] == "c":
                require(coverage[nid] == 1, f"coverage {node['name']}: {coverage[nid]}")
        gini = sum(abs(a-b) for a in actives for b in actives)/(2*len(actives)*sum(actives)) if sum(actives)>1e-6 else 0.0
        near(float(result["vehicles"]), len(actives), "vehicles")
        near(float(result["distance"]), total_distance, "solution distance")
        near(float(result["gini"]), gini, "solution Gini")
        near(float(result["max_time"]), max(durations, default=0), "solution MaxTime")
        reports.append(dict(solution=sid, valid=not errors, errors=errors,
                            vehicles=len(actives), distance=total_distance, gini=gini,
                            max_time=max(durations, default=0)))
    result_ids = {r["solution"] for r in reports}
    if set(schedules) != result_ids:
        raise RuntimeError("Exported schedule/result solution IDs differ")
    return dict(instance=str(instance), output=str(output),
                instance_sha256=hashlib.sha256(instance.read_bytes()).hexdigest(),
                solutions=len(reports), routes=sum(len(r) for r in schedules.values()),
                invalid=sum(not r["valid"] for r in reports),
                errors=sum(len(r["errors"]) for r in reports), reports=reports)


def check_run(output):
    output = Path(output)
    try:
        manifest = json.loads((output/"run.json").read_text(encoding="utf-8"))
        if hashlib.sha256((output/"input.txt").read_bytes()).hexdigest()!=manifest["input_sha256"]:
            raise ValueError("Input snapshot hash mismatch")
        report = validate(output/"input.txt", output)
        report["valid"] = bool(report["solutions"]) and report["invalid"]==0
        report["method"] = "Independent replay of exported schedules; no repair/rescheduling; delivery-only EVRPTW-PR"
    except Exception as error:
        report = dict(valid=False, solutions=0, routes=0, invalid=1, errors=1, reports=[], error=str(error))
    text = json.dumps(report, indent=2, allow_nan=False)+"\n"
    temporary = output/"validation.json.tmp"
    temporary.write_text(text, encoding="utf-8")
    temporary.replace(output/"validation.json")
    lines = ["INDEPENDENT REPLAY: "+("VALID" if report["valid"] else "INVALID"),
             f"Solutions: {report['solutions']}; routes: {report['routes']}; invalid solutions: {report['invalid']}"]
    if "error" in report:
        lines.append(report["error"])
    for solution in report["reports"]:
        for error in solution["errors"]:
            lines.append(f"solution {solution['solution']}: {error}")
    (output/"validation.txt").write_text("\n".join(lines)+"\n", encoding="utf-8")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Check an experiment's actual exported schedule independently")
    parser.add_argument("--run", type=Path, required=True)
    args = parser.parse_args()
    result = check_run(args.run)
    print(json.dumps({k:v for k,v in result.items() if k!="reports"}, indent=2))
    raise SystemExit(0 if result["valid"] else 3)
