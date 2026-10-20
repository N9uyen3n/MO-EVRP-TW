"""Repeat a noisy case using the already frozen executables; Linux CPU + wall time."""
import json
from pathlib import Path
import resource
import shutil
import statistics
import subprocess
import sys
import time
from run_ejection_undo_audit import dump, objectives, sha
from validate_audit_schedule import validate

def repeat(initial,target,case="r104_21",seed=2):
    initial=initial.resolve(); target=target.resolve(); target.mkdir(exist_ok=False)
    original=json.loads((initial/"manifest.json").read_text()); assert original["status"]=="completed"
    modes=("before","full","selective")
    prior=[r for r in original["runs"] if r["case"]==case and r["seed"]==seed and r["mode"] in modes]
    assert len(prior)==3
    executables={name:initial/f"ALNSAuditRunner_{name}" for name in ("before","after")}
    data=initial/f"{case}.txt"
    source=Path(__file__); shutil.copy2(source,target/source.name)
    report=dict(status="running",case=case,seed=seed,iterations=original["iterations"],
        original_manifest=str(initial/"manifest.json"),original_manifest_sha256=sha(initial/"manifest.json"),
        executable_sha256={name:sha(p) for name,p in executables.items()},input_sha256=sha(data),
        repeat_script_sha256=sha(source),original_runs=prior,runs=[])
    dump(target/"manifest.json",report)
    anchor=initial/f"{case}_s{seed}_before"
    for round_index,order in enumerate((("full","before","selective"),("selective","before","full")),1):
        for mode in order:
            out=target/f"round{round_index}_{mode}"
            args=[str(executables["before" if mode=="before" else "after"]),str(data),str(out),"3600",str(seed),str(original["iterations"]),"0","0","2"]
            if mode!="before": args.append(str(int(mode=="selective")))
            start_cpu=resource.getrusage(resource.RUSAGE_CHILDREN); start=time.monotonic()
            with (target/f"{out.name}.log").open("x") as log:
                subprocess.run(args,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=3700)
            wall=time.monotonic()-start; end_cpu=resource.getrusage(resource.RUSAGE_CHILDREN)
            cpu=end_cpu.ru_utime-start_cpu.ru_utime+end_cpu.ru_stime-start_cpu.ru_stime
            validation=validate(data,out); dump(out/"independent_validation.json",validation)
            assert validation["solutions"] and validation["invalid"]==0,out
            stats=json.loads((out/"run_statistics.json").read_text())
            assert stats["iterations"]==original["iterations"] and stats["stop_reason"]=="iteration_limit"
            assert objectives(out)==objectives(anchor) and sha(out/"schedules.csv")==sha(anchor/"schedules.csv"),out
            undo=None
            if mode!="before":
                undo=json.loads((out/"ejection_undo.json").read_text())
                reference=next(r["undo"] for r in prior if r["mode"]==mode)
                assert undo==reference,(mode,"search effort changed")
            report["runs"].append(dict(round=round_index,mode=mode,wall_seconds=wall,cpu_seconds=cpu,
                user_seconds=end_cpu.ru_utime-start_cpu.ru_utime,system_seconds=end_cpu.ru_stime-start_cpu.ru_stime,
                solutions=validation["solutions"],routes=validation["routes"],invalid=0,undo=undo))
            dump(target/"manifest.json",report)
            print(f"{out.name}: wall={wall:.3f}s cpu={cpu:.3f}s valid={validation['solutions']}",flush=True)
    report["wall_medians_three"]={mode:statistics.median([r["wall_seconds"] for r in prior+report["runs"] if r["mode"]==mode]) for mode in modes}
    report["cpu_medians_two"]={mode:statistics.median(r["cpu_seconds"] for r in report["runs"] if r["mode"]==mode) for mode in modes}
    assert report["executable_sha256"]=={name:sha(p) for name,p in executables.items()}
    assert report["input_sha256"]==sha(data) and report["repeat_script_sha256"]==sha(source)
    report["status"]="completed"; dump(target/"manifest.json",report)

if __name__=="__main__": repeat(*map(Path,sys.argv[1:3]))
