"""P6: paired fixed-iteration audit of conservative insertion time bounds (WSL)."""
import json
from pathlib import Path
import platform
import resource
import shutil
import subprocess
import sys
import time
import zipfile

from run_total_speed_audit import check, dump, objective_rows, sha, verify_source
from validate_audit_schedule import validate


def audit(before, after, output, iterations=200, repetitions=3):
    check(iterations > 0 and repetitions > 0, "Positive budgets required")
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    evidence = Path("artifacts/alns_p5/ejection_undo_20261020/experiment")
    previous = json.loads((evidence/"manifest.json").read_text())
    check(previous["status"] == "completed", "Incomplete baseline evidence")
    check(sha(before) == previous["executable_sha256"]["after"], "Unexpected baseline binary")
    baseline = verify_source("aebc888", evidence, output/"source_baseline.zip")
    names = subprocess.check_output(["git", "ls-files", "CMakeLists.txt", "include", "src"], text=True).splitlines()
    # TestALNS.cpp is the user's editable experiment, not part of this audit.
    names = [n for n in names if n != "src/test/TestALNS.cpp"]
    names += ["src/test/InsertionTimeBoundsTests.cpp", "src/test/run_insertion_bounds_audit.py",
              "src/test/summarize_insertion_bounds_audit.py"]
    source_hashes = {n: sha(Path(n)) for n in sorted(set(names))}
    with zipfile.ZipFile(output/"source_current.zip", "w", zipfile.ZIP_DEFLATED) as archive:
        for name in source_hashes:
            archive.write(name, name)
    binaries = {}
    for mode, path in (("baseline", before), ("current", after)):
        binaries[mode] = output/f"ALNSAuditRunner_{mode}"
        shutil.copy2(path, binaries[mode])
        binaries[mode].chmod(0o755)
    flags = Path("build-audit-wsl-p6-bounds/CMakeFiles/alns_lib.dir/flags.make")
    check("-DALNS_INSERTION_TIME_BOUNDS=1" in flags.read_text(), "P6 must be compiled on")
    shutil.copy2(flags, output/"current_flags.make")
    shutil.copy2(Path("build-audit-wsl-ejection-undo/CMakeFiles/alns_lib.dir/flags.make"), output/"baseline_flags.make")
    def optimization_flags(path):
        return next(line for line in path.read_text().splitlines() if line.startswith("CXX_FLAGS ="))
    check(optimization_flags(output/"current_flags.make") == optimization_flags(output/"baseline_flags.make"),
          "Compiler optimization flags differ")
    data = {}
    cases = ["r104_21", "r101_21", "c109_21"]
    for case in cases:
        data[case] = output/f"{case}.txt"
        shutil.copy2(Path("data/test-100")/f"{case}.txt", data[case])
    validator = Path(__file__).with_name("validate_audit_schedule.py")
    shutil.copy2(validator, output/validator.name)
    manifest = dict(status="running", baseline="aebc888 (validated P5)", current="P6 insertion time bounds ON",
        baseline_provenance=baseline, source_current_sha256=sha(output/"source_current.zip"),
        source_files_sha256=source_hashes, executable_sha256={k:sha(v) for k,v in binaries.items()},
        input_sha256={k:sha(v) for k,v in data.items()}, validator_sha256=sha(validator),
        iterations=iterations, repetitions=repetitions, cases=cases, seeds=[1,2], seconds=3600,
        charging_repair=False, profiling=False, scatter_search=False, reconstruction_cache=2, selective_ejection=1,
        environment=dict(platform=platform.platform(), python=sys.version,
            compiler=subprocess.check_output(["c++", "--version"], text=True)),
        counterbalance="adjacent alternating AB/BA; rotating case order", warmups=[], runs=[], comparisons=[])
    dump(output/"manifest.json", manifest)
    anchors = {}

    def execute(case, seed, mode, folder, count):
        args = [str(binaries[mode]), str(data[case]), str(folder), "3600", str(seed), str(count), "0", "0", "2", "1"]
        cpu0 = resource.getrusage(resource.RUSAGE_CHILDREN)
        start = time.monotonic()
        with (output/f"{folder.name}.log").open("x") as log:
            subprocess.run(args, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=3700)
        elapsed = time.monotonic()-start
        cpu1 = resource.getrusage(resource.RUSAGE_CHILDREN)
        report = validate(data[case], folder)
        dump(folder/"independent_validation.json", report)
        check(report["solutions"] > 0 and report["invalid"] == 0, f"Invalid output: {folder}")
        check(not (folder/"validation.txt").read_text().strip(), f"C++ validation: {folder}")
        stats = json.loads((folder/"run_statistics.json").read_text())
        check(stats["iterations"] == count and stats["stop_reason"] == "iteration_limit", stats)
        config = json.loads((folder/"audit_parameters.json").read_text())
        check(config["seed"] == seed and config["max_iterations"] == count and
              config["max_time_ms"] == 3600000 and config["charging_repair"] == 0 and
              config["profile_local_search"] == 0 and config["reconstruction_cache_mode"] == 2 and
              config["selective_ejection"] == 1, config)
        bounds = None
        if mode == "current":
            bounds = json.loads((folder/"insertion_time_bounds.json").read_text())
            check(bounds["enabled"] and bounds["requests"] > 0 and bounds["rejections"] > 0, bounds)
        return dict(case=case, seed=seed, mode=mode, output=folder.name, wall_seconds=elapsed,
            cpu_seconds=cpu1.ru_utime-cpu0.ru_utime+cpu1.ru_stime-cpu0.ru_stime,
            stats=stats, bounds=bounds, solutions=report["solutions"], routes=report["routes"], invalid=0)

    for mode in ("baseline", "current"):
        row = execute(cases[0], 123, mode, output/f"warmup_{mode}", 25)
        manifest["warmups"].append(row)
        dump(output/"manifest.json", manifest)
        print(f"warmup {mode}: {row['wall_seconds']:.3f}s", flush=True)
    for rep in range(1, repetitions+1):
        case_order = cases[(rep-1)%len(cases):]+cases[:(rep-1)%len(cases)]
        for case in case_order:
            for seed in manifest["seeds"]:
                order = ["baseline", "current"] if (rep+cases.index(case)+seed)%2 == 0 else ["current", "baseline"]
                rows = {}
                for mode in order:
                    folder = output/f"{case}_s{seed}_rep{rep}_{mode}"
                    row = execute(case, seed, mode, folder, iterations)
                    row.update(repetition=rep, position=order.index(mode))
                    fingerprint = (objective_rows(folder), sha(folder/"schedules.csv"),
                        json.loads((folder/"reconstruction_cache.json").read_text()),
                        json.loads((folder/"ejection_undo.json").read_text()))
                    key = (case, seed)
                    if key not in anchors:
                        anchors[key] = fingerprint
                    check(anchors[key] == fingerprint, f"Changed objective/schedule: {folder}")
                    rows[mode] = row
                    manifest["runs"].append(row)
                    dump(output/"manifest.json", manifest)
                    print(f"{folder.name}: wall={row['wall_seconds']:.3f}s valid={row['solutions']} bounds={row['bounds']}", flush=True)
                manifest["comparisons"].append(dict(case=case, seed=seed, repetition=rep, order=order,
                    identical_objectives=True, byte_identical_schedules=True,
                    identical_reconstruction_and_ejection_counters=True,
                    reduction_percent=100*(1-rows["current"]["wall_seconds"]/rows["baseline"]["wall_seconds"])))
                dump(output/"manifest.json", manifest)
    check(manifest["executable_sha256"] == {k:sha(v) for k,v in binaries.items()}, "Binaries changed")
    check(manifest["input_sha256"] == {k:sha(v) for k,v in data.items()}, "Inputs changed")
    check(source_hashes == {n:sha(Path(n)) for n in source_hashes}, "Source changed during benchmark")
    check(manifest["validator_sha256"] == sha(validator), "Validator changed")
    manifest["status"] = "completed"
    dump(output/"manifest.json", manifest)


if __name__ == "__main__":
    audit(*map(Path,sys.argv[1:4]), iterations=int(sys.argv[4]) if len(sys.argv)>4 else 200,
          repetitions=int(sys.argv[5]) if len(sys.argv)>5 else 3)
