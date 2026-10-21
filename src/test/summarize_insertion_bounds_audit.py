"""Summarize the complete P6 audit without dropping slower samples."""
import json
from pathlib import Path
import sys

from summarize_total_speed_audit import summarize as summarize_pairs


def summarize(root):
    result=summarize_pairs(root)
    manifest=json.loads((root/"manifest.json").read_text())
    if any(not c["identical_reconstruction_and_ejection_counters"] for c in manifest["comparisons"]):
        raise RuntimeError("Changed reconstruction or recursive search")
    counters={}
    for case in manifest["cases"]:
        samples=[r["bounds"] for r in manifest["runs"] if r["mode"]=="current" and r["case"]==case]
        totals={k:sum(s[k] for s in samples) for k in ("requests","builds","rejections")}
        totals["rejection_percent"]=100*totals["rejections"]/totals["requests"]
        counters[case]=totals
    result["insertion_time_bounds"]=counters
    result["limitations"]=[
        "Baseline is validated P5 aebc888; this measures the additional P6 effect, not total speedup from the original code.",
        "Two algorithm seeds per instance; timing repetitions do not add independent seeds.",
        f"{manifest['iterations']} configured iterations set adaptive thresholds; this is not a prefix of a 25000-iteration run.",
        "All paired samples retained; WSL and host load can affect wall and CPU time.",
        "Charging repair, profiling and Scatter Search off; reconstruction cache and selective undo on in both versions.",
        "The relaxation only rejects impossible insertion times; it neither certifies battery feasibility nor optimizes charging globally.",
        "Bounds cache assumes immutable instance data while routes are in use, as does the existing route evaluator.",
        "Compilation uses an explicit P6 switch; changing the CMake default does not update existing build caches."]
    return result


if __name__=="__main__":
    result=summarize(Path(sys.argv[1]))
    with Path(sys.argv[2]).open("x",encoding="utf-8") as handle:
        json.dump(result,handle,indent=2)
        handle.write("\n")
    print(json.dumps({k:v for k,v in result.items() if k not in ("cases","samples")},indent=2))
