"""Paired ejection rollback audit: old executable/full checkpoints/selective undo."""
import csv
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time
from validate_audit_schedule import validate

def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def dump(p,v): p.write_text(json.dumps(v,indent=2)+"\n",encoding="utf-8")
def objectives(out):
    with (out/"results.csv").open(newline="") as handle:
        return [{k:v for k,v in row.items() if k!="elapsed_ms"} for row in csv.DictReader(handle)]

def audit(before,after,root,iterations=200):
    target=root.resolve()/"experiment"; target.mkdir(exist_ok=False)
    binaries={}
    for name,source in (("before",before),("after",after)):
        dest=target/f"ALNSAuditRunner_{name}"; shutil.copy2(source,dest); dest.chmod(0o755); binaries[name]=dest
    sources=[Path("CMakeLists.txt")]+sorted(p for base in ("include","src") for p in Path(base).rglob("*")
        if p.suffix in (".h",".cpp",".py") and "__pycache__" not in p.parts)
    hashes={str(p):sha(p) for p in sources}
    for p in sources:
        dest=target/"source"/p; dest.parent.mkdir(parents=True,exist_ok=True); shutil.copy2(p,dest)
    manifest=dict(status="running",iterations=iterations,seconds=3600,seeds=[1,2],
        cases=["r104_21","r101_21","c109_21"],charging_repair=False,reconstruction_cache=2,
        git_head=subprocess.check_output(["git","rev-parse","HEAD"],text=True).strip(),source_sha256=hashes,
        executable_sha256={m:sha(p) for m,p in binaries.items()},runs=[],comparisons=[])
    dump(target/"manifest.json",manifest)
    for case in manifest["cases"]:
        data=target/f"{case}.txt"; shutil.copy2(Path("data/test-100")/data.name,data)
        for seed in manifest["seeds"]:
            modes=(["profile_full","profile_selective"] if seed==1 else [])+["before"]+(
                ["full","selective"] if seed==1 else ["selective","full"])
            outputs={}; reference=None
            for mode in modes:
                out=target/f"{case}_s{seed}_{mode}"; profiling=mode.startswith("profile_")
                selective=mode.endswith("selective")
                args=[str(binaries["before" if mode=="before" else "after"]),str(data),str(out),"3600",str(seed),str(iterations),"0",str(int(profiling)),"2"]
                if mode!="before": args.append(str(int(selective)))
                start=time.monotonic()
                with (target/f"{out.name}.log").open("x") as log:
                    subprocess.run(args,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=3700)
                wall=time.monotonic()-start
                validation=validate(data,out); dump(out/"independent_validation.json",validation)
                assert validation["solutions"] and validation["invalid"]==0,out
                stats=json.loads((out/"run_statistics.json").read_text())
                assert stats["iterations"]==iterations and stats["stop_reason"]=="iteration_limit",stats
                undo=None
                if mode!="before":
                    undo=json.loads((out/"ejection_undo.json").read_text())
                    effort={k:undo[k] for k in ("checkpoints","rollbacks","commits","place_calls","branch_trials")}
                    if reference is None: reference=effort
                    assert reference==effort,(mode,reference,effort)
                    assert undo["checkpoints"]==undo["rollbacks"]+undo["commits"]
                    if selective: assert undo["full_snapshots"]==0 and undo["peak_undo_routes"]<=6,undo
                    else: assert undo["full_snapshots"]==undo["checkpoints"],undo
                    if profiling:
                        metrics=json.loads((out/"local_search_profile.json").read_text())["metrics"]
                        roots=metrics["run"]["inclusive_ns"]+metrics["intensified_reduction"]["inclusive_ns"]
                        assert roots==sum(m["self_ns"] for m in metrics.values())
                outputs[mode]=out
                manifest["runs"].append(dict(case=case,seed=seed,mode=mode,wall_seconds=wall,stats=stats,undo=undo,
                    solutions=validation["solutions"],routes=validation["routes"],invalid=0,input_sha256=sha(data)))
                dump(target/"manifest.json",manifest)
                print(f"{out.name}: {wall:.3f}s; valid={validation['solutions']}; saved_routes={undo['saved_routes'] if undo else 'legacy'}",flush=True)
            assert all(objectives(p)==objectives(outputs["before"]) for p in outputs.values()),(case,seed,"objectives")
            assert len({sha(p/"schedules.csv") for p in outputs.values()})==1,(case,seed,"schedules")
            manifest["comparisons"].append(dict(case=case,seed=seed,modes=list(outputs),
                identical_objectives=True,byte_identical_schedules=True,identical_search_effort=True))
            dump(target/"manifest.json",manifest)
    assert hashes=={str(p):sha(p) for p in sources},"Sources changed during audit"
    assert manifest["executable_sha256"]=={m:sha(p) for m,p in binaries.items()}
    manifest["status"]="completed"; dump(target/"manifest.json",manifest)

if __name__=="__main__": audit(*map(Path,sys.argv[1:4]),iterations=int(sys.argv[4]) if len(sys.argv)>4 else 200)
