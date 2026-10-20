"""Frozen Release binaries: observe repetitions, then paired cache off/on audit."""
import csv
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time

from validate_audit_schedule import validate


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def dump(path, value):
    path.write_text(json.dumps(value, indent=2)+"\n", encoding="utf-8")


def objectives(out):
    with (out/"results.csv").open(newline="") as handle:
        return [{k:v for k,v in row.items() if k != "elapsed_ms"} for row in csv.DictReader(handle)]


def audit(before, after, root, iterations=200):
    target = root.resolve()/"experiment"
    target.mkdir(exist_ok=False)
    executables = {}
    for name, source in (("before", before), ("after", after)):
        copy = target/f"ALNSAuditRunner_{name}"
        shutil.copy2(source, copy); copy.chmod(0o755); executables[name] = copy
    sources = [Path("CMakeLists.txt")]+sorted(p for base in ("include", "src")
        for p in Path(base).rglob("*") if p.suffix in (".h", ".cpp", ".py") and "__pycache__" not in p.parts)
    source_hashes = {str(p):sha(p) for p in sources}
    for p in sources:
        dest = target/"source"/p; dest.parent.mkdir(parents=True, exist_ok=True); shutil.copy2(p,dest)
    manifest = dict(status="running", iterations=iterations, seconds=3600, seeds=[1,2],
        cases=["r104_21","r101_21","c109_21"], charging_repair=False,
        git_head=subprocess.check_output(["git","rev-parse","HEAD"],text=True).strip(),
        source_sha256=source_hashes, executable_sha256={m:sha(p) for m,p in executables.items()}, runs=[], comparisons=[])
    dump(target/"manifest.json",manifest)
    for case in manifest["cases"]:
        data=target/f"{case}.txt"; shutil.copy2(Path("data/test-100")/data.name,data)
        for seed in manifest["seeds"]:
            # Observe is diagnostic, excluded from runtime comparisons. Other
            # runs disable profiling; alternate off/on order to reduce bias.
            modes=(["observe"] if seed==1 else [])+["before"]+(["off","on"] if seed==1 else ["on","off"])
            outputs={}
            for mode in modes:
                out=target/f"{case}_s{seed}_{mode}"
                binary=executables["before" if mode=="before" else "after"]
                args=[str(binary),str(data),str(out),"3600",str(seed),str(iterations),"0",str(int(mode=="observe"))]
                if mode!="before": args.extend([str({"off":0,"observe":1,"on":2}[mode]),"0"])
                start=time.monotonic()
                with (target/f"{out.name}.log").open("x") as log:
                    subprocess.run(args,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=3700)
                wall=time.monotonic()-start
                result=validate(data,out); dump(out/"independent_validation.json",result)
                assert result["solutions"] and result["invalid"]==0,out
                stats=json.loads((out/"run_statistics.json").read_text())
                assert stats["iterations"]==iterations and stats["stop_reason"]=="iteration_limit",stats
                cache=None
                if mode!="before":
                    cache=json.loads((out/"reconstruction_cache.json").read_text())
                    assert cache["requests"]==cache["computations"]+cache["hits"],cache
                    assert cache["computations"]==cache["successes"]+cache["failures"],cache
                    assert cache["peak_entries"]<=cache["capacity"]<=4096,cache
                    if mode!="on": assert cache["hits"]==0,cache
                    if mode=="observe":
                        profile=json.loads((out/"local_search_profile.json").read_text())["metrics"]
                        roots=profile["run"]["inclusive_ns"]+profile["intensified_reduction"]["inclusive_ns"]
                        assert roots==sum(e["self_ns"] for e in profile.values()),out
                        assert profile["reconstruction"]["calls"]>=cache["requests"]
                manifest["runs"].append(dict(case=case,seed=seed,mode=mode,wall_seconds=wall,stats=stats,
                    cache=cache,input_sha256=sha(data),solutions=result["solutions"],routes=result["routes"],invalid=0))
                outputs[mode]=out; dump(target/"manifest.json",manifest)
                print(f"{out.name}: {wall:.3f}s; valid={result['solutions']}; hits={cache['hits'] if cache else 0}",flush=True)
            assert all(objectives(p)==objectives(outputs["before"]) for p in outputs.values()),(case,seed,"objectives")
            assert len({sha(p/"schedules.csv") for p in outputs.values()})==1,(case,seed,"schedules")
            if "observe" in outputs:
                a=json.loads((outputs["observe"]/"reconstruction_cache.json").read_text())
                b=json.loads((outputs["on"]/"reconstruction_cache.json").read_text())
                assert a["requests"]==b["requests"] and a["repeated_failures"]==b["hits"],(a,b)
            manifest["comparisons"].append(dict(case=case,seed=seed,modes=list(outputs),
                identical_objectives=True,byte_identical_schedules=True))
            dump(target/"manifest.json",manifest)
    assert source_hashes=={str(p):sha(p) for p in sources},"Source changed during experiment"
    assert manifest["executable_sha256"]=={m:sha(p) for m,p in executables.items()}
    manifest["status"]="completed"; dump(target/"manifest.json",manifest)


if __name__=="__main__":
    audit(*map(Path,sys.argv[1:4]),iterations=int(sys.argv[4]) if len(sys.argv)>4 else 200)
