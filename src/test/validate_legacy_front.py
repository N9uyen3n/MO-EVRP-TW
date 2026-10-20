"""Audit a rounded legacy Pareto log without calling the solver/evaluator.

Uses original Euclidean Solomon data and scipy/HiGHS linear programs for
fixed-node-order partial charging. Customer deadlines apply to service start;
charging must finish by station closing. No pickup, no station/order changes.
"""
import argparse
import csv
import hashlib
import json
import math
import re
from collections import Counter
from pathlib import Path

import numpy as np
from scipy.optimize import linprog

ROUND = 0.00500001  # Legacy detail log has two decimal places.


def read_instance(path):
    nodes, params = {}, {}
    for line in path.read_text(encoding="utf-8-sig").splitlines()[1:]:
        if "/" in line:
            params[line.strip()[0]] = float(line.split("/")[1])
        else:
            fields = line.split()
            if len(fields) == 8 and fields[1] in ("d", "c", "f"):
                name, kind = fields[:2]
                x, y, demand, ready, due, service = map(float, fields[2:])
                nodes[name] = dict(kind=kind, x=x, y=y, demand=demand,
                                   ready=ready, due=due, service=service)
    assert len([n for n in nodes.values() if n["kind"] == "d"]) == 1
    assert all(k in params for k in "QCrgv")
    return nodes, params


def read_front(path):
    solutions, solution, route = [], None, None
    for lineno, line in enumerate(path.read_text(encoding="utf-8-sig").splitlines(), 1):
        match = re.match(r"== Solution ID: (\d+)", line)
        if match:
            solution = dict(id=int(match[1]), routes=[])
            solutions.append(solution)
        match = re.match(r"--- Route ID: (\d+) ---", line)
        if match:
            route = dict(id=int(match[1]), line=lineno, rows=[])
            solution["routes"].append(route)
        match = re.match(r"--- Node Sequence \(Count: (\d+)\) ---", line)
        if match:
            route["expected_count"] = int(match[1])
        if re.match(r"\s*\d+\s*\|", line):
            fields = [part.strip() for part in line.split("|")]
            assert len(fields) == 8
            route["rows"].append(dict(node=fields[1], arrival=float(fields[3]),
                                      departure=float(fields[4]), battery=float(fields[5]),
                                      load=float(fields[6]), charge=float(fields[7]), line=lineno))
    assert solutions and all(s["routes"] for s in solutions)
    assert all(len(r["rows"]) == r["expected_count"] for s in solutions for r in s["routes"])
    return solutions


def distance(nodes, a, b):
    return math.hypot(nodes[a]["x"]-nodes[b]["x"], nodes[a]["y"]-nodes[b]["y"])


def lp_schedule(rows, nodes, p, mode):
    """mode: free, actions (rounded times/charges), or all (also rounded battery).

    Variables per visit: departure time, departure battery, charged energy.
    Arrival is previous departure plus travel. Optional waiting is allowed.
    Every charging arrival must have nonnegative battery before replenishing.
    """
    n, width = len(rows), 3*len(rows)
    T, B, Q = lambda i: i, lambda i: n+i, lambda i: 2*n+i
    eq, rhs_eq, ub, rhs_ub, bounds = [], [], [], [], []
    for i, row in enumerate(rows):
        node = nodes[row["node"]]
        hi = node["due"] + (node["service"] if node["kind"] == "c" else 0)
        bounds.append((node["ready"] + (node["service"] if node["kind"] == "c" else 0), hi))
    bounds += [(0, p["Q"])]*n
    bounds += [(0, p["Q"]) if nodes[r["node"]]["kind"] == "f" else (0, 0) for r in rows]

    def constraint(rhs, coeffs, is_eq=False):
        vec = np.zeros(width)
        for index, val in coeffs.items():
            vec[index] = val
        (eq if is_eq else ub).append(vec)
        (rhs_eq if is_eq else rhs_ub).append(rhs)

    def intersect(index, lo, hi):
        oldlo, oldhi = bounds[index]
        bounds[index] = (max(lo, oldlo), min(hi, oldhi))

    bounds[T(0)] = (nodes[rows[0]["node"]]["ready"],)*2
    bounds[B(0)] = (p["Q"],)*2
    for i in range(1, n):
        a, b = rows[i-1]["node"], rows[i]["node"]
        edge = distance(nodes, a, b)
        travel, energy = edge/p["v"], edge*p["r"]
        node = nodes[b]
        constraint(-energy, {B(i): 1, B(i-1): -1, Q(i): -1}, True)
        constraint(-energy, {B(i-1): -1})
        if node["kind"] == "c":
            constraint(-travel-node["service"], {T(i-1): 1, T(i): -1})
        elif node["kind"] == "f":
            constraint(-travel, {T(i-1): 1, T(i): -1, Q(i): p["g"]})
            constraint(-node["ready"], {Q(i): p["g"], T(i): -1})
        else:
            constraint(travel, {T(i): 1, T(i-1): -1}, True)
    if mode != "free":
        for i, row in enumerate(rows):
            intersect(T(i), row["departure"]-ROUND, row["departure"]+ROUND)
            intersect(Q(i), row["charge"]-ROUND, row["charge"]+ROUND)
            if mode == "all":
                intersect(B(i), row["battery"]-ROUND, row["battery"]+ROUND)
            if i:
                travel = distance(nodes, rows[i-1]["node"], row["node"])/p["v"]
                intersect(T(i-1), row["arrival"]-ROUND-travel, row["arrival"]+ROUND-travel)
    if any(lo > hi for lo, hi in bounds):
        return dict(feasible=False, reason="inconsistent variable bounds after rounding")
    objective = np.zeros(width)
    objective[:n] = 1  # Prefer early schedules; does not optimize algorithm objectives.
    result = linprog(objective, A_ub=np.array(ub) if ub else None, b_ub=rhs_ub or None,
                     A_eq=np.array(eq) if eq else None, b_eq=rhs_eq or None,
                     bounds=bounds, method="highs")
    if not result.success:
        assert result.status == 2, result.message
        return dict(feasible=False, reason=result.message)
    # Independently replay the chosen charges with earliest feasible service.
    clock, battery = nodes[rows[0]["node"]]["ready"], p["Q"]
    schedule, wait, charge, total_distance = [], 0.0, 0.0, 0.0
    for i, row in enumerate(rows):
        node = nodes[row["node"]]
        q = max(0.0, float(result.x[Q(i)]))
        if i:
            edge = distance(nodes, rows[i-1]["node"], row["node"])
            total_distance += edge
            clock += edge/p["v"]
            battery -= edge*p["r"]
        assert battery >= -1e-6
        arrival = clock
        waiting = max(0.0, node["ready"]-clock)
        clock += waiting
        wait += waiting
        assert clock <= node["due"]+1e-6
        if node["kind"] == "f":
            battery += q
            assert battery <= p["Q"]+1e-6
            clock += q*p["g"]
            charge += q
            assert clock <= node["due"]+1e-6
        elif node["kind"] == "c":
            clock += node["service"]
        schedule.append(dict(node=row["node"], arrival=arrival, departure=clock,
                             charge=q, battery=battery, wait=waiting))
    duration = clock-nodes[rows[0]["node"]]["ready"]
    return dict(feasible=True, schedule=schedule, distance=total_distance,
                duration=duration, active=duration-wait, wait=wait, charge=charge)


def audit_route(route, nodes, p):
    rows, errors = route["rows"], []

    def error(kind, row, **detail):
        errors.append(dict(kind=kind, line=row["line"], node=row["node"], **detail))

    assert len(rows) >= 2 and rows[0]["node"] == rows[-1]["node"] == "D0"
    assert all(r["node"] in nodes for r in rows)
    assert all(nodes[r["node"]]["kind"] != "d" for r in rows[1:-1])
    load = p["C"]
    for i, row in enumerate(rows):
        node = nodes[row["node"]]
        if node["kind"] == "c":
            load -= node["demand"]
        if load < -1e-7 or abs(row["load"]-load) > ROUND:
            error("load", row, expected=load, logged=row["load"])
        if row["battery"] < -ROUND or row["battery"] > p["Q"]+ROUND:
            error("battery_range", row, logged=row["battery"], capacity=p["Q"])
        if max(row["arrival"], node["ready"]) > node["due"]+ROUND:
            error("time_window", row, logged_arrival=row["arrival"], due=node["due"])
        if node["kind"] == "f" and row["departure"] > node["due"]+ROUND:
            error("station_closing", row, logged_departure=row["departure"], due=node["due"])
        if i:
            prev = rows[i-1]
            edge = distance(nodes, prev["node"], row["node"])
            expected = prev["battery"]-edge*p["r"]+row["charge"]
            if abs(row["battery"]-expected) > 3*ROUND:
                error("battery_conservation", row, previous=prev["node"],
                      expected_from_previous=expected, logged=row["battery"])
            if prev["battery"]-edge*p["r"] < -ROUND:
                error("negative_arrival_battery", row, previous=prev["node"],
                      computed=prev["battery"]-edge*p["r"])
            expected_arrival = prev["departure"]+edge/p["v"]
            if abs(row["arrival"]-expected_arrival) > 2*ROUND:
                error("travel_time", row, expected=expected_arrival, logged=row["arrival"])
        expected_departure = max(row["arrival"], node["ready"])
        expected_departure += row["charge"]*p["g"] if node["kind"] == "f" else node["service"]
        if abs(row["departure"]-expected_departure) > 2*ROUND+ROUND*p["g"]:
            error("departure_time", row, expected=expected_departure, logged=row["departure"])
    return errors


def gini(values):
    return sum(abs(a-b) for a in values for b in values)/(2*len(values)*sum(values)) if sum(values) else 0


def run(instance, folder, output):
    output.mkdir(parents=True, exist_ok=False)
    nodes, params = read_instance(instance)
    front_file = folder/(folder.name+"_front_details.txt")
    objectives_file = folder/(folder.name+"_front_objectives.csv")
    solutions = read_front(front_file)
    objectives = {int(r["SolutionID"]): r for r in csv.DictReader(objectives_file.open())}
    assert len(objectives) == len(solutions)
    free_cache, action_cache, all_cache, reports = {}, {}, {}, []
    schedules_file = (output/"reconstructed_schedules.csv").open("w", newline="")
    witness = csv.DictWriter(schedules_file, fieldnames=["solution", "route_index", "position", "node",
                                                      "arrival", "departure", "charge", "battery", "wait"])
    witness.writeheader()
    for sol in solutions:
        sid, routes, served = sol["id"], [], Counter()
        for ri, route in enumerate(sol["routes"], 1):
            rows = route["rows"]
            sequence = tuple(r["node"] for r in rows)
            served.update(r["node"] for r in rows if nodes[r["node"]]["kind"] == "c")
            errors = audit_route(route, nodes, params)
            if sequence not in free_cache:
                free_cache[sequence] = lp_schedule(rows, nodes, params, "free")
            actions_key = tuple((r["node"], r["arrival"], r["departure"], r["charge"]) for r in rows)
            if actions_key not in action_cache:
                action_cache[actions_key] = lp_schedule(rows, nodes, params, "actions")
            all_key = tuple((r["node"], r["arrival"], r["departure"], r["charge"], r["battery"]) for r in rows)
            if all_key not in all_cache:
                all_cache[all_key] = lp_schedule(rows, nodes, params, "all")
            free = free_cache[sequence]
            if free["feasible"]:
                for pos, state in enumerate(free["schedule"]):
                    witness.writerow(dict(solution=sid, route_index=ri, position=pos, **state))
            routes.append(dict(index=ri, route_id=route["id"], line=route["line"], nodes=sequence,
                               direct_errors=errors, rounded_full_schedule_feasible=all_cache[all_key]["feasible"],
                               rounded_actions_feasible=action_cache[actions_key]["feasible"],
                               fixed_sequence_feasible=free["feasible"],
                               reconstructed={k: v for k, v in free.items() if k != "schedule"}))
        coverage = {name: served[name] for name, node in nodes.items()
                    if node["kind"] == "c" and served[name] != 1}
        capacity_ok = all(sum(nodes[n]["demand"] for n in r["nodes"] if nodes[n]["kind"] == "c")
                          <= params["C"]+1e-7 for r in routes)
        fixed_ok = not coverage and capacity_ok and all(r["fixed_sequence_feasible"] for r in routes)
        actual_distance = sum(distance(nodes, r["nodes"][i-1], r["nodes"][i])
                              for r in routes for i in range(1, len(r["nodes"])))
        reported = objectives[sid]
        metrics = None
        if fixed_ok:
            metrics = dict(vehicles=len(routes), distance=actual_distance,
                           gini=gini([r["reconstructed"]["active"] for r in routes]),
                           max_time=max(r["reconstructed"]["duration"] for r in routes))
        reports.append(dict(solution=sid, reported=reported, coverage_errors=coverage, capacity_ok=capacity_ok,
                            distance_recomputed=actual_distance,
                            distance_matches=abs(actual_distance-float(reported["TotalDistance"])) <= 0.00005001,
                            rounded_full_schedule_feasible=not coverage and capacity_ok and all(r["rounded_full_schedule_feasible"] for r in routes),
                            rounded_actions_feasible=not coverage and capacity_ok and all(r["rounded_actions_feasible"] for r in routes),
                            fixed_sequence_feasible=fixed_ok, reconstructed_metrics=metrics, routes=routes))
    schedules_file.close()
    counts = Counter(e["kind"] for s in reports for r in s["routes"] for e in r["direct_errors"])
    summary = dict(solutions=len(reports), routes=sum(len(s["routes"]) for s in reports),
                   rounded_full_schedule_feasible=sum(s["rounded_full_schedule_feasible"] for s in reports),
                   rounded_actions_feasible=sum(s["rounded_actions_feasible"] for s in reports),
                   fixed_sequence_feasible=sum(s["fixed_sequence_feasible"] for s in reports),
                   coverage_ok=sum(not s["coverage_errors"] for s in reports),
                   capacity_ok=sum(s["capacity_ok"] for s in reports),
                   distance_matches=sum(s["distance_matches"] for s in reports),
                   direct_error_counts=dict(counts), unique_node_sequences=len(free_cache))
    result = dict(instance=str(instance), log_folder=str(folder), parameters=params,
                  source_hashes={str(f): hashlib.sha256(f.read_bytes()).hexdigest()
                                 for f in (instance, front_file, objectives_file)},
                  method="Independent Euclidean replay plus fixed-order linear partial-charging feasibility, two-decimal rounding intervals +/-0.00500001; optional waiting allowed; station completion <= closing; no node additions/removals/reordering",
                  summary=summary, solutions=reports)
    (output/"validation.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    fields = ["solution", "vehicles", "distance", "reported_gini", "reported_max_time",
              "rounded_full_schedule_feasible", "rounded_actions_feasible", "fixed_sequence_feasible",
              "reconstructed_gini", "reconstructed_max_time", "bad_logged_routes", "infeasible_sequences"]
    with (output/"summary.csv").open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for s in reports:
            m = s["reconstructed_metrics"] or {}
            writer.writerow(dict(solution=s["solution"], vehicles=len(s["routes"]), distance=s["distance_recomputed"],
                                 reported_gini=s["reported"]["WorkloadGini"], reported_max_time=s["reported"]["MaxTime"],
                                 rounded_full_schedule_feasible=s["rounded_full_schedule_feasible"],
                                 rounded_actions_feasible=s["rounded_actions_feasible"],
                                 fixed_sequence_feasible=s["fixed_sequence_feasible"],
                                 reconstructed_gini=m.get("gini"), reconstructed_max_time=m.get("max_time"),
                                 bad_logged_routes=sum(not r["rounded_full_schedule_feasible"] for r in s["routes"]),
                                 infeasible_sequences=sum(not r["fixed_sequence_feasible"] for r in s["routes"])))
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("instance", type=Path)
    parser.add_argument("folder", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    run(args.instance, args.folder, args.output)
