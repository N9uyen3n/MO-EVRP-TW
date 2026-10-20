"""Summarize full-snapshot versus selective-undo audit; no nested time double counting."""
import json
from pathlib import Path
import statistics
import sys

def summarize(root):
    m=json.loads((root/"manifest.json").read_text())
    assert m["status"]=="completed" and len(m["runs"])==24 and len(m["comparisons"])==6
    assert all(r["invalid"]==0 for r in m["runs"])
    cases={}; reductions=[]; vs_before=[]
    for case in m["cases"]:
        pairs=[]
        for seed in m["seeds"]:
            rows={r["mode"]:r for r in m["runs"] if r["case"]==case and r["seed"]==seed}
            a,b=rows["full"],rows["selective"]
            delta=100*(1-b["wall_seconds"]/a["wall_seconds"]); reductions.append(delta)
            before_delta=100*(1-b["wall_seconds"]/rows["before"]["wall_seconds"]); vs_before.append(before_delta)
            pairs.append(dict(seed=seed,before_seconds=rows["before"]["wall_seconds"],full_seconds=a["wall_seconds"],
                selective_seconds=b["wall_seconds"],reduction_percent=delta,vs_before_percent=before_delta,
                full_undo=a["undo"],selective_undo=b["undo"]))
        profiles={}
        for mode in ("profile_full","profile_selective"):
            run=next(r for r in m["runs"] if r["case"]==case and r["mode"]==mode)
            p=json.loads((root/f"{case}_s1_{mode}"/"local_search_profile.json").read_text())["metrics"]
            total=p["run"]["inclusive_ns"]+p["intensified_reduction"]["inclusive_ns"]
            copy=p["ejection_snapshot"]["inclusive_ns"]; restore=p["ejection_restore"]["inclusive_ns"]
            profiles[mode]=dict(ls_seconds=total/1e9,ejection_seconds=p["ejection"]["inclusive_ns"]/1e9,
                snapshot_seconds=copy/1e9,restore_seconds=restore/1e9,copy_restore_ls_percent=100*(copy+restore)/total,undo=run["undo"])
        full=sum(p["full_seconds"] for p in pairs); selective=sum(p["selective_seconds"] for p in pairs)
        saved_full=sum(p["full_undo"]["saved_routes"] for p in pairs)
        saved_selective=sum(p["selective_undo"]["saved_routes"] for p in pairs)
        cases[case]=dict(pairs=pairs,profiles=profiles,full_total_seconds=full,selective_total_seconds=selective,
            reduction_percent=100*(1-selective/full),full_saved_routes=saved_full,selective_saved_routes=saved_selective,
            saved_routes_reduction_percent=100*(1-saved_selective/saved_full) if saved_full else 0)
    return dict(source_manifest=str(root/"manifest.json"),iterations=m["iterations"],runs=24,identical_groups=6,
        solutions=sum(r["solutions"] for r in m["runs"]),routes=sum(r["routes"] for r in m["runs"]),invalid=0,
        median_reduction_percent=statistics.median(reductions),min_reduction_percent=min(reductions),max_reduction_percent=max(reductions),
        median_vs_before_percent=statistics.median(vs_before),cases=cases,
        limitations=["Two seeds, 200 configured iterations, charging repair off, reconstruction cache on.",
            "Timing comparisons exclude profiling runs; wall time subject to system noise.",
            "Counters and timers cover Stage B checkpoints only; other ejection route copies unchanged.",
            "New full reference restores vector by move, while legacy copied it back.",
            "No claim of universal runtime gains or global optimization."])

if __name__=="__main__":
    result=summarize(Path(sys.argv[1]))
    with Path(sys.argv[2]).open("x",encoding="utf-8") as handle: json.dump(result,handle,indent=2); handle.write("\n")
    print(json.dumps({k:v for k,v in result.items() if k!="cases"},indent=2))
