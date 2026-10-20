"""Summarize fixed-iteration cache comparisons; diagnostic runs excluded from speed."""
import json
from pathlib import Path
import statistics
import sys


def summarize(root):
    manifest=json.loads((root/"manifest.json").read_text())
    assert manifest["status"]=="completed" and len(manifest["runs"])==21
    assert len(manifest["comparisons"])==6 and all(r["invalid"]==0 for r in manifest["runs"])
    cases={}; reductions=[]
    for case in manifest["cases"]:
        pairs=[]; hits=requests=0
        for seed in manifest["seeds"]:
            rows={r["mode"]:r for r in manifest["runs"] if r["case"]==case and r["seed"]==seed}
            off,on=rows["off"],rows["on"]
            reduction=100*(1-on["wall_seconds"]/off["wall_seconds"]); reductions.append(reduction)
            hits+=on["cache"]["hits"]; requests+=on["cache"]["requests"]
            pairs.append(dict(seed=seed,before_seconds=rows["before"]["wall_seconds"],
                off_seconds=off["wall_seconds"],on_seconds=on["wall_seconds"],
                reduction_percent=reduction,speedup=off["wall_seconds"]/on["wall_seconds"],cache=on["cache"]))
        diagnostic=next(r for r in manifest["runs"] if r["case"]==case and r["mode"]=="observe")
        profile=json.loads((root/f"{case}_s1_observe"/"local_search_profile.json").read_text())["metrics"]
        ls=profile["run"]["inclusive_ns"]+profile["intensified_reduction"]["inclusive_ns"]
        off=sum(p["off_seconds"] for p in pairs); on=sum(p["on_seconds"] for p in pairs)
        cases[case]=dict(pairs=pairs,off_total_seconds=off,on_total_seconds=on,
            reduction_percent=100*(1-on/off),speedup=off/on,hits=hits,requests=requests,
            hit_percent=100*hits/requests if requests else 0,
            diagnostic=dict(reconstruction_seconds=profile["reconstruction"]["inclusive_ns"]/1e9,
                reconstruction_ls_percent=100*profile["reconstruction"]["inclusive_ns"]/ls,
                cache=diagnostic["cache"]))
    return dict(source_manifest=str(root/"manifest.json"),iterations=manifest["iterations"],
        runs=21,identical_groups=6,solutions=sum(r["solutions"] for r in manifest["runs"]),
        routes=sum(r["routes"] for r in manifest["runs"]),invalid=0,
        median_reduction_percent=statistics.median(reductions),
        min_reduction_percent=min(reductions),max_reduction_percent=max(reductions),cases=cases,
        limitations=["Two seeds per case; not a statistical claim for all instances or long runs.",
            "200 is the configured iteration budget, including adaptive search thresholds.",
            "Charging repair disabled; deadline-bound subroutines can differ under wall-time budgets.",
            "Observe runs have profiling enabled and are excluded from timing comparisons.",
            "Cache stores heuristic failures, not proofs of route infeasibility.",
            "Instance data immutable within each local search entry point."])


if __name__=="__main__":
    result=summarize(Path(sys.argv[1]))
    with Path(sys.argv[2]).open("x",encoding="utf-8") as handle:
        json.dump(result,handle,indent=2); handle.write("\n")
    print(json.dumps({k:v for k,v in result.items() if k!="cases"},indent=2))
