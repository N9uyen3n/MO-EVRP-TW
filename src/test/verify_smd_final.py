"""Final integrity check after all SMD experiments, without rerunning the solver."""
import argparse
import io
import json
from pathlib import Path
import subprocess
import zipfile

from freeze_smd_stage import sha
from run_smd_audit import objectives, require
from summarize_smd_audit import summarize
from verify_smd_cache_skips import verify


PRODUCTION=("include/","src/core/","src/alns/","src/io/","src/utils/","src/logger/","src/app/")
EXPERIMENTS={"S1_scope":20,"S2_equivalence":20,"S3_observe":20,"S4_enabled":20,"S5_speed":60,"S5_quality":50,"S5_rc205_repeat":12}


def check(root, build, user_source_sha):
    experiments={}
    warmup_count=0
    for name, count in EXPERIMENTS.items():
        summary=summarize(root/name)
        require(summary["runs"] == count and summary["invalid"] == 0, f"Wrong run count: {name}")
        experiments[name]={key:summary[key] for key in ("runs","solutions","routes","invalid","identical_pairs","manifest_sha256",
                                                       "warmup_solutions","warmup_routes")}
        manifest=json.loads((root/name/"manifest.json").read_text())
        warmup_count+=len(manifest["warmups"])
        for record in manifest["runs"]+manifest["warmups"]:
            folder=root/name/record["output"]
            require(json.loads((folder/"run_statistics.json").read_text()) == record["stats"], "Raw run statistics changed")
            if "or_opt_cache" in record:
                require(json.loads((folder/"or_opt_cache.json").read_text()) == record["or_opt_cache"], "Raw cache counters changed")
                if manifest["kind"]=="speed":
                    cache=record["or_opt_cache"]
                    require(cache["mode"]==0 and cache["bytes"]==cache["lookups"]==cache["skips"]==0,
                            "Default speed experiment used an active Or-opt cache")
        if manifest["kind"] in ("equivalence","speed"):
            for pair in manifest["pairs"]:
                prefix=f"{pair['case']}_s{pair['seed']}_rep{pair['repetition']}_"
                for filename in ("ejection_undo.json","reconstruction_cache.json"):
                    require(sha(root/name/(prefix+"before")/filename) == sha(root/name/(prefix+"after")/filename),
                            f"Deterministic work counters changed: {name}/{prefix}/{filename}")
        if manifest["warmup"]:
            require(len(manifest["warmups"])==2, "Missing speed warmups")
            before,after=(root/name/f"warmup_{mode}" for mode in ("before","after"))
            require(objectives(before)==objectives(after) and sha(before/"schedules.csv")==sha(after/"schedules.csv"),
                    "Warmup behavior changed")
    for stage in ("B0","B1","B2","B3"):
        metadata=json.loads((root/stage/"stage.json").read_text())
        for name, digest in metadata["files"].items():
            require(sha(root/stage/name) == digest, f"Stage modified: {stage}/{name}")
    baseline=json.loads((root/"B3"/"stage.json").read_text())
    recorded=json.loads((root/"S3_observe"/"manifest.json").read_text())["stages"]["after"]
    require(baseline==recorded,"B3 stage metadata changed since observation")
    relevant=lambda name: (name.startswith(PRODUCTION) and name.endswith((".h",".cpp"))) or name in (
        "CMakeLists.txt","src/test/ALNSAuditRunner.cpp","src/test/ScheduleValidator.h")
    checked=[]
    with zipfile.ZipFile(io.BytesIO((root/"B3"/"source.zip").read_bytes())) as archive:
        for name in archive.namelist():
            if relevant(name):
                require(Path(name).read_bytes().replace(b"\r\n",b"\n") == archive.read(name).replace(b"\r\n",b"\n"),
                        f"Current production differs from measured B3: {name}")
                checked.append(name)
    live={str(path).replace("\\","/") for directory in PRODUCTION for path in Path(directory).rglob("*")
          if path.is_file() and path.suffix in (".h",".cpp")}
    require(live == {name for name in checked if name.startswith(PRODUCTION)}, "Added or removed production files")
    require(sha(build/"ALNSAuditRunner") == baseline["files"]["ALNSAuditRunner"], "Current audit executable differs")
    require(sha(build/"CMakeFiles/alns_lib.dir/flags.make") == baseline["files"]["flags.make"], "Current compiler flags differ")
    require(sha(Path("src/test/TestALNS.cpp")) == user_source_sha, "User TestALNS changed since the final app build; rebuild latest source")
    apps={name:sha(build/name) for name in ("ALNSAuditRunner","TestALNS","untitled")}
    cache=verify(root/"S3_observe",root/"S4_enabled")
    # Derived reports must agree with the unchanged raw evidence too.
    reports={"S1_scope":"alns_smd_s1_scope_results","S2_equivalence":"alns_smd_s2_results",
             "S3_observe":"alns_smd_s3_results","S4_enabled":"alns_smd_s4_results",
             "S5_speed":"alns_smd_speed_results","S5_quality":"alns_smd_quality_results",
             "S5_rc205_repeat":"alns_smd_rc205_repeat_results"}
    for name, stem in reports.items():
        saved=json.loads((Path("update")/f"{stem}.json").read_text())
        require(saved == summarize(root/name), f"Derived report differs: {stem}")
    return dict(valid=True,solver_revision=baseline["revision"],
                git_head=subprocess.check_output(["git","rev-parse","HEAD"],text=True).strip(),
                checked_current_source_files=len(checked),default_or_opt_cache="Off",
                experiments=experiments,apps_sha256=apps,user_test_source_sha256=user_source_sha,
                exact_cache_skip_validation=cache["valid"],
                experiment_runs=sum(e["runs"] for e in experiments.values()),warmup_runs=warmup_count,
                exported_solutions=sum(e["solutions"]+e["warmup_solutions"] for e in experiments.values()),
                exported_routes=sum(e["routes"]+e["warmup_routes"] for e in experiments.values()),
                note="Export counts include repeats across versions/seeds/repetitions; timing protocols are not pooled.")


if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root",type=Path); parser.add_argument("build",type=Path)
    parser.add_argument("output",type=Path)
    parser.add_argument("--user-source-sha",default="9f0db18f208a65ccda7c0018faf492022d00512ca18a2f5714f241d9eb125a11")
    args=parser.parse_args()
    result=check(args.root,args.build,args.user_source_sha)
    args.output.write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(result,indent=2))
