"""Report direct baseline/current comparisons, including every measured pair."""
import json
from pathlib import Path
import statistics
import sys

def summarize(root):
    m=json.loads((root/"manifest.json").read_text())
    expected=len(m["cases"])*len(m["seeds"])*m["repetitions"]
    if m["status"]!="completed" or len(m["runs"])!=2*expected or len(m["comparisons"])!=expected:
        raise RuntimeError("Incomplete experiment")
    if any(r["invalid"] for r in m["runs"]+m["warmups"]): raise RuntimeError("Invalid schedule")
    pairs=[]; cases={}
    for c in m["comparisons"]:
        if not c["identical_objectives"] or not c["byte_identical_schedules"]: raise RuntimeError("Changed search output")
        rows={r["mode"]:r for r in m["runs"] if (r["case"],r["seed"],r["repetition"])==(c["case"],c["seed"],c["repetition"])}
        a,b=rows["baseline"],rows["current"]
        pairs.append(dict(case=c["case"],seed=c["seed"],repetition=c["repetition"],order=c["order"],
            baseline_seconds=a["wall_seconds"],current_seconds=b["wall_seconds"],
            baseline_cpu_seconds=a["cpu_seconds"],current_cpu_seconds=b["cpu_seconds"],
            reduction_percent=100*(1-b["wall_seconds"]/a["wall_seconds"]),
            cpu_reduction_percent=100*(1-b["cpu_seconds"]/a["cpu_seconds"]),
            speedup=a["wall_seconds"]/b["wall_seconds"]))
    for case in m["cases"]:
        selected=[p for p in pairs if p["case"]==case]
        seeds={}
        for seed in m["seeds"]:
            samples=[p for p in selected if p["seed"]==seed]
            a=statistics.median(p["baseline_seconds"] for p in samples); b=statistics.median(p["current_seconds"] for p in samples)
            seeds[str(seed)]=dict(baseline_median_seconds=a,current_median_seconds=b,reduction_of_medians_percent=100*(1-b/a),
                median_paired_reduction_percent=statistics.median(p["reduction_percent"] for p in samples))
        cases[case]=dict(seeds=seeds,median_paired_reduction_percent=statistics.median(p["reduction_percent"] for p in selected),
            faster_pairs=sum(p["reduction_percent"]>0 for p in selected),pairs=len(selected))
    a=sum(p["baseline_seconds"] for p in pairs); b=sum(p["current_seconds"] for p in pairs)
    reductions=[p["reduction_percent"] for p in pairs]
    return dict(source_manifest=str(root/"manifest.json"),baseline=m["baseline"],current=m["current"],iterations=m["iterations"],
        repetitions=m["repetitions"],measured_runs=len(m["runs"]),warmup_runs=len(m["warmups"]),pairs=len(pairs),
        solutions=sum(r["solutions"] for r in m["runs"]),routes=sum(r["routes"] for r in m["runs"]),invalid=0,
        warmup_solutions=sum(r["solutions"] for r in m["warmups"]),warmup_routes=sum(r["routes"] for r in m["warmups"]),
        median_paired_reduction_percent=statistics.median(reductions),min_paired_reduction_percent=min(reductions),
        max_paired_reduction_percent=max(reductions),faster_pairs=sum(v>0 for v in reductions),slower_pairs=sum(v<0 for v in reductions),
        baseline_total_seconds=a,current_total_seconds=b,total_time_reduction_percent=100*(1-b/a),
        median_cpu_reduction_percent=statistics.median(p["cpu_reduction_percent"] for p in pairs),cases=cases,samples=pairs,
        limitations=["Baseline is the validated pre-speed-optimization revision 226f943, not the older faulty evaluator.",
            "Two seeds per instance, repeated timing samples do not add independent algorithm seeds.",
            "200 configured iterations include adaptive thresholds; not the prefix of a 25000-iteration run.",
            "All measured pairs included; wall and CPU time remain affected by system and WSL conditions.",
            "Charging repair/profiling/scatter disabled; current reconstruction cache and selective undo enabled."])

if __name__=="__main__":
    result=summarize(Path(sys.argv[1]))
    with Path(sys.argv[2]).open("x",encoding="utf-8") as handle: json.dump(result,handle,indent=2); handle.write("\n")
    print(json.dumps({k:v for k,v in result.items() if k not in ("cases","samples")},indent=2))
