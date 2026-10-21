"""P7 quality comparison: prioritize fleet, keep all seeds and report soft deadlines."""
import csv
import json
import math
from pathlib import Path
import statistics
import sys

from summarize_charging_repair_audit import hypervolume3d


def summarize(root):
    manifest = json.loads((root/"manifest.json").read_text())
    expected = len(manifest["cases"])*len(manifest["seeds"])*2
    preflight_count = len(manifest["cases"])*len(manifest["preflight_seeds"])*2
    if (manifest["status"] != "completed" or len(manifest["runs"]) != expected or
            len(manifest["preflight"]) != preflight_count or len(manifest["comparisons"]) != preflight_count//2):
        raise RuntimeError("Incomplete P7 experiment")
    records = manifest["runs"]
    if any(r["invalid"] or r["solutions"] < 1 for r in records+manifest["preflight"]):
        raise RuntimeError("Invalid or empty output")
    if any(not c["identical_objectives"] or not c["byte_identical_schedules"] for c in manifest["comparisons"]):
        raise RuntimeError("Preflight changed search behavior")
    index = {(r["case"],r["seed"],r["mode"]):r for r in records}
    if len(index) != expected:
        raise RuntimeError("Duplicate runs")
    pairs, aggregate, references = [], {}, {}
    for case in manifest["cases"]:
        selected = [r for r in records if r["case"] == case]
        fleet = min(int(r["best"]["vehicles"]) for r in selected)
        meta = manifest["instances"][case]
        horizon = meta["horizon"]
        scale = (fleet*horizon*meta["parameters"]["v"], 1.0, horizon)
        references[case] = dict(fleet=fleet, normalization=scale, reference=[1.1]*3)
        hv = {}
        for r in selected:
            rows = list(csv.DictReader((root/r["output"]/"results.csv").open()))
            points = [(float(s["distance"])/scale[0],float(s["gini"]),float(s["max_time"])/horizon)
                      for s in rows if int(s["vehicles"]) == fleet]
            hv[r["seed"],r["mode"]] = hypervolume3d(points) if points else None
        for seed in manifest["seeds"]:
            a, b = (index[case,seed,m] for m in ("baseline", "current"))
            av, bv = (int(r["best"]["vehicles"]) for r in (a,b))
            ad, bd = (float(r["best"]["distance"]) for r in (a,b))
            outcome = "win" if bv < av or (bv == av and bd < ad-1e-6) else (
                "loss" if bv > av or (bv == av and bd > ad+1e-6) else "tie")
            ah, bh = hv[seed,"baseline"], hv[seed,"current"]
            pairs.append(dict(case=case, seed=seed, order=a["pair_order"],
                baseline_fleet=av, current_fleet=bv, baseline_distance=ad, current_distance=bd,
                distance_reduction_percent=100*(1-bd/ad) if av == bv else None,
                baseline_gini=float(a["best"]["gini"]), current_gini=float(b["best"]["gini"]),
                baseline_max_time=float(a["best"]["max_time"]), current_max_time=float(b["best"]["max_time"]),
                baseline_hv3d=ah, current_hv3d=bh,
                hv_outcome=("win" if bh > ah+1e-12 else "loss" if bh < ah-1e-12 else "tie") if ah is not None and bh is not None else None,
                outcome=outcome, baseline_iterations=a["stats"]["iterations"], current_iterations=b["stats"]["iterations"],
                baseline_wall_seconds=a["wall_seconds"], current_wall_seconds=b["wall_seconds"],
                baseline_stop=a["stats"]["stop_reason"], current_stop=b["stats"]["stop_reason"]))
        aggregate[case] = {}
        for mode in ("baseline", "current"):
            subset = [r for r in selected if r["mode"] == mode]
            aggregate[case][mode] = dict(fleets=[int(r["best"]["vehicles"]) for r in subset],
                best_fleet=min(int(r["best"]["vehicles"]) for r in subset),
                median_fleet=statistics.median(int(r["best"]["vehicles"]) for r in subset),
                common_best_fleet_hits=sum(int(r["best"]["vehicles"]) == fleet for r in subset),
                representative_distance_median=statistics.median(float(r["best"]["distance"]) for r in subset),
                median_iterations=statistics.median(r["stats"]["iterations"] for r in subset),
                median_wall_seconds=statistics.median(r["wall_seconds"] for r in subset),
                median_initialization_ms=statistics.median(r["stats"]["initialization_ms"] for r in subset),
                median_search_ms=statistics.median(r["stats"]["search_ms"] for r in subset),
                maximum_search_overshoot_ms=max(r["search_overshoot_ms"] for r in subset),
                maximum_total_wall_excess_seconds=max(r["total_wall_excess_seconds"] for r in subset),
                stop_reasons={reason:sum(r["stats"]["stop_reason"] == reason for r in subset)
                              for reason in sorted({r["stats"]["stop_reason"] for r in subset})})
    return dict(source_manifest=str(root/"manifest.json"), revisions=manifest["revisions"],
        quality_runs=len(records), preflight_runs=len(manifest["preflight"]), search_seconds=manifest["seconds"],
        solutions=sum(r["solutions"] for r in records), routes=sum(r["routes"] for r in records), invalid=0,
        preflight_solutions=sum(r["solutions"] for r in manifest["preflight"]),
        preflight_routes=sum(r["routes"] for r in manifest["preflight"]),
        wins=sum(p["outcome"] == "win" for p in pairs), losses=sum(p["outcome"] == "loss" for p in pairs),
        ties=sum(p["outcome"] == "tie" for p in pairs),
        fleet_wins=sum(p["current_fleet"] < p["baseline_fleet"] for p in pairs),
        fleet_losses=sum(p["current_fleet"] > p["baseline_fleet"] for p in pairs),
        hv_wins=sum(p["hv_outcome"] == "win" for p in pairs), hv_losses=sum(p["hv_outcome"] == "loss" for p in pairs),
        hv_ties=sum(p["hv_outcome"] == "tie" for p in pairs), hv_incomparable=sum(p["hv_outcome"] is None for p in pairs),
        aggregate=aggregate, pairs=pairs, hv_reference_boxes=references,
        limitations=["P7 is closed within the user-selected additional RC scope, not a comprehensive publication benchmark.",
            "Five paired seeds on rc106/rc205 only; older R/C evidence has different protocols and is reported separately.",
            "Time budget applies to search after initialization; deadline is soft and actual overshoot is retained.",
            "Different iteration counts/fronts are expected under time limits; this is not a fixed-work speed comparison.",
            "Fleet has absolute priority; representative selects fleet then distance, not simultaneous improvement of all objectives.",
            "HV3D uses common physical normalization at the instance-wide best fleet; higher fleet runs have null HV.",
            "Charging repair/profiling/Scatter Search disabled; no solver changes made for this audit."])


if __name__ == "__main__":
    if not math.isclose(hypervolume3d([(0,0,0)],(1,1,1)),1.0):
        raise RuntimeError("HV origin check")
    if not math.isclose(hypervolume3d([(0,.5,.5),(.5,0,0)],(1,1,1)),.625):
        raise RuntimeError("HV union check")
    result = summarize(Path(sys.argv[1]))
    destination = Path(sys.argv[2])
    with destination.open("x",encoding="utf-8") as handle:
        json.dump(result,handle,indent=2)
        handle.write("\n")
    with destination.with_suffix(".csv").open("x",newline="") as handle:
        writer=csv.DictWriter(handle,fieldnames=list(result["pairs"][0]))
        writer.writeheader()
        writer.writerows(result["pairs"])
    print(json.dumps({k:v for k,v in result.items() if k not in ("aggregate","pairs")},indent=2))
