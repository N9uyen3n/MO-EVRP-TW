"""Cross-check real Enabled skips against the prior full Observe traversal."""
import argparse
import json
from pathlib import Path

from freeze_smd_stage import sha
from run_smd_audit import objectives, require


def verify(observe, enabled):
    first=json.loads((observe/"manifest.json").read_text())
    second=json.loads((enabled/"manifest.json").read_text())
    require(first["status"] == second["status"] == "completed", "Incomplete cache experiment")
    require(first["mode_settings"]["after"] == 1 and second["mode_settings"]["after"] == 2, "Expected Observe/Enabled")
    require(first["stages"]["after"]["files"]["ALNSAuditRunner"] == second["stages"]["after"]["files"]["ALNSAuditRunner"], "Different cache binary")
    require(first["iterations"] == second["iterations"], "Different configured iteration budgets")
    sources=[{(r["case"],r["seed"],r["repetition"]):r for r in m["runs"] if r["mode"] == "after"} for m in (first,second)]
    require(set(sources[0]) == set(sources[1]), "Different case/seed sets")
    stable=("sessions","lookups","repeated_blocks","inserts","evictions","peak_entries",
            "successful_blocks","generation_changes","topology_rebuilds","capacity","bytes")
    pairs=[]
    for identity in sorted(sources[0]):
        a,b=(source[identity] for source in sources)
        left,right=a["or_opt_cache"],b["or_opt_cache"]
        require(left["mismatches"] == right["mismatches"] == 0, "Cache mismatch")
        require(all(left[field] == right[field] for field in stable), f"Different traversal/policy counters: {identity}")
        require(right["skips"] == left["repeated_blocks"] and left["skips"] == 0, "Enabled skip count")
        require(left["exhausted_blocks"]-right["exhausted_blocks"] == right["skips"], "Exhaustion accounting")
        require(left["candidate_positions"]-right["candidate_positions"] == left["repeated_candidate_positions"], "Candidate skip accounting")
        require(left["evaluations"]-right["evaluations"] == left["repeated_evaluations"], "Evaluation skip accounting")
        require(right["repeated_candidate_positions"] == right["repeated_evaluations"] == 0, "Hit body executed in Enabled")
        folders=observe/a["output"],enabled/b["output"]
        require(objectives(folders[0]) == objectives(folders[1]) and
                sha(folders[0]/"schedules.csv") == sha(folders[1]/"schedules.csv"), "Observe/Enabled outcomes changed")
        pairs.append(dict(case=identity[0],seed=identity[1],repetition=identity[2],skipped_blocks=right["skips"],
                          saved_candidates=left["repeated_candidate_positions"],saved_evaluations=left["repeated_evaluations"],
                          observed_candidates=left["candidate_positions"],observed_evaluations=left["evaluations"]))
    return dict(valid=True,pairs=pairs,source_manifests={"observe":sha(observe/"manifest.json"),"enabled":sha(enabled/"manifest.json")},
                skipped_blocks=sum(p["skipped_blocks"] for p in pairs),saved_candidates=sum(p["saved_candidates"] for p in pairs),
                saved_evaluations=sum(p["saved_evaluations"] for p in pairs),
                note="Exact work accounting at fixed configured iterations, not a runtime speedup claim.")


if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("observe",type=Path); parser.add_argument("enabled",type=Path)
    parser.add_argument("output",type=Path)
    args=parser.parse_args()
    report=verify(args.observe,args.enabled)
    args.output.write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    print(json.dumps({k:v for k,v in report.items() if k!="pairs"},indent=2))
