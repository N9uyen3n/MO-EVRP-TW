"""Direct paired benchmark of 226f943 versus edc8e92, using frozen Release binaries."""
import csv
import hashlib
import io
import json
from pathlib import Path
import platform
import resource
import shutil
import subprocess
import sys
import time
import zipfile

from validate_audit_schedule import validate

REVISIONS = {"baseline": "226f943", "current": "edc8e92"}
EVIDENCE = {
    "baseline": Path("artifacts/alns_p5/profiling_20261020/experiment"),
    "current": Path("artifacts/alns_p5/ejection_undo_20261020/experiment"),
}
PRODUCTION = ("include/", "src/core/", "src/alns/", "src/io/", "src/utils/", "src/logger/", "src/app/")

def check(condition, message):
    if not condition:
        raise RuntimeError(str(message))

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def dump(path, value):
    path.write_text(json.dumps(value, indent=2)+"\n", encoding="utf-8")

def objective_rows(output):
    with (output/"results.csv").open(newline="") as handle:
        return [{k:v for k,v in row.items() if k != "elapsed_ms"} for row in csv.DictReader(handle)]

def verify_source(revision, evidence, target):
    archive = subprocess.check_output(["git", "archive", "--format=zip", revision, "--", "CMakeLists.txt", "include", "src"])
    target.write_bytes(archive)
    checked = 0
    with zipfile.ZipFile(io.BytesIO(archive)) as files:
        for name in files.namelist():
            relevant = name in ("CMakeLists.txt", "src/test/ALNSAuditRunner.cpp", "src/test/ScheduleValidator.h") or (
                name.startswith(PRODUCTION) and name.endswith((".h", ".cpp")))
            if not relevant:
                continue
            raw = evidence/"source"/name
            check(raw.exists(), f"Missing frozen source: {raw}")
            check(raw.read_bytes().replace(b"\r\n", b"\n") == files.read(name).replace(b"\r\n", b"\n"), f"Source differs from Git: {name}")
            checked += 1
    return dict(revision=subprocess.check_output(["git", "rev-parse", revision], text=True).strip(),
                verified_production_files=checked, archive_sha256=sha(target))

def audit(before, after, output, iterations=200, repetitions=3):
    check(iterations>0 and repetitions>0, "Positive iterations and repetitions required")
    output = output.resolve(); output.mkdir(parents=True, exist_ok=False)
    binaries = {}; provenance = {}
    for mode, source in (("baseline", before), ("current", after)):
        evidence = EVIDENCE[mode]
        old = json.loads((evidence/"manifest.json").read_text())
        check(old["status"] == "completed", f"Incomplete provenance: {evidence}")
        check(sha(source) == old["executable_sha256"]["after"], f"Executable does not match frozen source evidence: {source}")
        copied = output/f"ALNSAuditRunner_{mode}"
        shutil.copy2(source, copied); copied.chmod(0o755); binaries[mode] = copied
        provenance[mode] = verify_source(REVISIONS[mode], evidence, output/f"source_{mode}.zip")
        provenance[mode].update(executable_sha256=sha(copied), evidence_manifest=str(evidence/"manifest.json"),
                                evidence_manifest_sha256=sha(evidence/"manifest.json"))
    cases = ["r104_21", "r101_21", "c109_21"]; data = {}
    for case in cases:
        data[case] = output/f"{case}.txt"; shutil.copy2(Path("data/test-100")/f"{case}.txt", data[case])
    script = Path(__file__); shutil.copy2(script, output/script.name)
    validator = Path(__file__).with_name("validate_audit_schedule.py")
    shutil.copy2(validator, output/validator.name)
    manifest = dict(status="running", baseline=REVISIONS["baseline"], current=REVISIONS["current"],
        iterations=iterations, repetitions=repetitions, seeds=[1,2], cases=cases, seconds=3600,
        profiling=False, charging_repair=False, scatter_search=False, provenance=provenance,
        current_reconstruction_cache=2, current_selective_ejection=1, counterbalance="alternate adjacent AB/BA pairs",
        input_sha256={case:sha(path) for case,path in data.items()},
        script_sha256=sha(script), validator_sha256=sha(validator),
        environment=dict(platform=platform.platform(), python=sys.version,
            compiler=subprocess.check_output(["c++", "--version"], text=True),
            cmake=subprocess.check_output(["cmake", "--version"], text=True)), warmups=[], runs=[], comparisons=[])
    dump(output/"manifest.json", manifest)
    anchors = {}
    def execute(case, seed, mode, folder, count):
        args = [str(binaries[mode]), str(data[case]), str(folder), "3600", str(seed), str(count), "0", "0"]
        if mode == "current": args.extend(["2", "1"])
        cpu0 = resource.getrusage(resource.RUSAGE_CHILDREN); start = time.monotonic()
        with (output/f"{folder.name}.log").open("x") as log:
            subprocess.run(args, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=3700)
        elapsed = time.monotonic()-start; cpu1 = resource.getrusage(resource.RUSAGE_CHILDREN)
        report = validate(data[case], folder); dump(folder/"independent_validation.json", report)
        check(report["solutions"]>0 and report["invalid"] == 0, f"Invalid output: {folder}")
        check((folder/"validation.txt").read_text().strip() == "", f"C++ validation failed: {folder}")
        stats = json.loads((folder/"run_statistics.json").read_text())
        check(stats["iterations"] == count and stats["stop_reason"] == "iteration_limit", stats)
        config = json.loads((folder/"audit_parameters.json").read_text())
        check(config["seed"] == seed and config["max_iterations"] == count and config["max_time_ms"] == 3600000, config)
        check(config["charging_repair"] == 0 and config["profile_local_search"] == 0, config)
        if mode == "current":
            check(config["reconstruction_cache_mode"] == 2 and config["selective_ejection"] == 1, config)
        return dict(case=case, seed=seed, mode=mode, output=folder.name, wall_seconds=elapsed,
            cpu_seconds=cpu1.ru_utime-cpu0.ru_utime+cpu1.ru_stime-cpu0.ru_stime,
            user_seconds=cpu1.ru_utime-cpu0.ru_utime, system_seconds=cpu1.ru_stime-cpu0.ru_stime,
            stats=stats, solutions=report["solutions"], routes=report["routes"], invalid=0)
    # Both binaries get a validated warmup, excluded from runtime summaries.
    for mode in ("baseline", "current"):
        row = execute(cases[0], 123, mode, output/f"warmup_{mode}", 25)
        manifest["warmups"].append(row); dump(output/"manifest.json", manifest)
        print(f"warmup {mode}: {row['wall_seconds']:.3f}s", flush=True)
    for rep in range(1, repetitions+1):
        case_order = cases[(rep-1)%len(cases):]+cases[:(rep-1)%len(cases)]
        for case in case_order:
            for seed in manifest["seeds"]:
                order = ["baseline", "current"] if (rep+cases.index(case)+seed)%2==0 else ["current", "baseline"]
                rows = {}
                for mode in order:
                    folder = output/f"{case}_s{seed}_rep{rep}_{mode}"
                    row = execute(case, seed, mode, folder, iterations); row.update(repetition=rep, position=order.index(mode))
                    fingerprint = (objective_rows(folder), sha(folder/"schedules.csv"))
                    key = (case, seed)
                    if key not in anchors: anchors[key] = fingerprint
                    check(anchors[key] == fingerprint, f"Search result changed across versions/repeats: {folder}")
                    rows[mode] = row; manifest["runs"].append(row); dump(output/"manifest.json", manifest)
                    print(f"{folder.name}: wall={row['wall_seconds']:.3f}s cpu={row['cpu_seconds']:.3f}s valid={row['solutions']}", flush=True)
                manifest["comparisons"].append(dict(case=case, seed=seed, repetition=rep, order=order,
                    identical_objectives=True, byte_identical_schedules=True,
                    reduction_percent=100*(1-rows["current"]["wall_seconds"]/rows["baseline"]["wall_seconds"])))
                dump(output/"manifest.json", manifest)
    for mode,path in binaries.items(): check(sha(path)==provenance[mode]["executable_sha256"], f"Binary changed: {mode}")
    check(manifest["input_sha256"] == {case:sha(path) for case,path in data.items()}, "Inputs changed")
    check(manifest["script_sha256"] == sha(script) and manifest["validator_sha256"] == sha(validator), "Audit tools changed")
    manifest["status"] = "completed"; dump(output/"manifest.json", manifest)

if __name__ == "__main__":
    audit(*map(Path,sys.argv[1:4]), iterations=int(sys.argv[4]) if len(sys.argv)>4 else 200,
          repetitions=int(sys.argv[5]) if len(sys.argv)>5 else 3)
