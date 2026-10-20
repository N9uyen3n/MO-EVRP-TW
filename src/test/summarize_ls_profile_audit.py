"""Summarize completed profiling audits without adding nested inclusive times."""
import json
from pathlib import Path
import statistics
import sys


def summarize(target):
    manifest = json.loads((target/"manifest.json").read_text())
    assert manifest["status"] == "completed"
    assert len(manifest["comparisons"]) == 6 and len(manifest["runs"]) == 18
    assert all(r["invalid"] == 0 for r in manifest["runs"])
    profiles = {}
    per_case = {}
    for case in manifest["instances"]:
        aggregate = {}
        timing = []
        for seed in manifest["seeds"]:
            rows = {r["mode"]:r for r in manifest["runs"] if r["case"] == case and r["seed"] == seed}
            profile = json.loads((target/f"{case}_s{seed}_on"/"local_search_profile.json").read_text())
            profiles[f"{case}_s{seed}"] = profile
            for name, entry in profile["metrics"].items():
                totals = aggregate.setdefault(name, dict.fromkeys(entry, 0))
                for key, value in entry.items():
                    totals[key] += value
            timing.append(dict(seed=seed, **{mode:row["wall_seconds"] for mode,row in rows.items()},
                               on_vs_off_percent=100*(rows["on"]["wall_seconds"]/rows["off"]["wall_seconds"]-1),
                               off_vs_before_percent=100*(rows["off"]["wall_seconds"]/rows["before"]["wall_seconds"]-1)))
        total = aggregate["run"]["inclusive_ns"]+aggregate["intensified_reduction"]["inclusive_ns"]
        phases = {name:100*aggregate[name]["inclusive_ns"]/total for name in
                  ("distance", "charging", "vehicle_reduction", "intensified_reduction")}
        per_case[case] = dict(ls_seconds=total/1e9, phase_percent=phases, metrics=aggregate, timing=timing,
                              self_percent={name:100*e["self_ns"]/total for name,e in aggregate.items()},
                              inclusive_percent={name:100*e["inclusive_ns"]/total for name,e in aggregate.items()})
    on_overhead = [t["on_vs_off_percent"] for c in per_case.values() for t in c["timing"]]
    off_overhead = [t["off_vs_before_percent"] for c in per_case.values() for t in c["timing"]]
    return dict(source_manifest=str(target/"manifest.json"), iterations=manifest["iterations"],
                runs=len(manifest["runs"]), identical_triplets=len(manifest["comparisons"]),
                solutions=sum(r["solutions"] for r in manifest["runs"]),
                routes=sum(r["routes"] for r in manifest["runs"]), invalid=0,
                median_on_vs_off_percent=statistics.median(on_overhead),
                median_off_vs_before_percent=statistics.median(off_overhead), cases=per_case,
                limitations=["Two seeds per case; fixed iterations, not a quality comparison.",
                             "Timing is monotonic wall time, includes profiling overhead and scheduling noise.",
                             "Inclusive columns overlap: do not sum parents and children.",
                             "evaluate_move excludes candidates checked directly by other helpers.",
                             "vnd_improvements counts successful VND searches, not vehicle reductions.",
                             "Charging repair disabled in this audit; no inference about its enabled overhead."])


if __name__ == "__main__":
    result = summarize(Path(sys.argv[1]))
    with Path(sys.argv[2]).open("x", encoding="utf-8") as handle:
        json.dump(result, handle, indent=2)
        handle.write("\n")
    print(json.dumps({k:v for k,v in result.items() if k != "cases"}, indent=2))
