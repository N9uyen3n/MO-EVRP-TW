"""Replay full-precision ALNSAuditRunner CSVs against raw Solomon input.

Independent of Route/evaluator and of the validity flags exported by the solver.
No repair, rounding adjustment, rescheduling, or optional optimization is used.
"""
import csv
import hashlib
import json
import math
import sys
from collections import Counter, defaultdict
from pathlib import Path


def validate(instance, output):
    nodes, params = [], {}
    for line in instance.read_text(encoding="utf-8-sig").splitlines()[1:]:
        if "/" in line:
            params[line.strip()[0]] = float(line.split("/")[1])
        else:
            f = line.split()
            if len(f) == 8 and f[1] in ("d", "f", "c"):
                nodes.append(dict(name=f[0], kind=f[1], x=float(f[2]), y=float(f[3]),
                                  demand=float(f[4]), ready=float(f[5]), due=float(f[6]),
                                  service=float(f[7])))
    schedules = defaultdict(lambda: defaultdict(list))
    for row in csv.DictReader((output/"schedules.csv").open()):
        schedules[int(row["solution"])][int(row["route"])].append(row)
    reports = []
    for result in csv.DictReader((output/"results.csv").open()):
        sid = int(result["solution"])
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


if __name__ == "__main__":
    instance, output = map(Path, sys.argv[1:3])
    report = validate(instance, output)
    if len(sys.argv)>3:
        target = Path(sys.argv[3])
        with target.open("x", encoding="utf-8") as handle:
            json.dump(report, handle, indent=2)
    print(json.dumps({k:v for k,v in report.items() if k != "reports"}, indent=2))
    raise SystemExit(0 if report["solutions"] and report["invalid"] == 0 else 3)
