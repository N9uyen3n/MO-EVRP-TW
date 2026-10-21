"""P7 scope agreed by the user: rc106/rc205, fixed-count preflight + 5 paired seeds.

No solver changes. Run under WSL, sequentially, with the validated frozen binaries.
"""
import argparse
import csv
import io
import json
from pathlib import Path
import platform
import resource
import shutil
import subprocess
import time
import zipfile

from run_total_speed_audit import check, objective_rows, sha, verify_source
from validate_audit_schedule import validate

CASES = ["rc106_21", "rc205_21"]
REVISIONS = {"baseline": "226f943", "current": "edd709b"}
HASHES = {"baseline": "59605d7c3fb148b167803b7d76d7819df1d6734085434ce00992014d28f31619",
          "current": "420016ebbd7dda5744da6b5ccee941fd3a770831a1484d1623f0c64709cddd25"}


def dump(path, value):
    temporary = path.with_suffix(path.suffix+".tmp")
    temporary.write_text(json.dumps(value, indent=2)+"\n", encoding="utf-8")
    temporary.replace(path)


def instance_metadata(path):
    nodes, params = [], {}
    for line in path.read_text(encoding="utf-8-sig").splitlines()[1:]:
        fields = line.split()
        if "/" in line:
            params[line.strip()[0]] = float(line.split("/")[1])
        elif len(fields) == 8 and fields[1] in ("d", "f", "c"):
            nodes.append(fields)
    counts = {kind:sum(n[1] == kind for n in nodes) for kind in ("d", "f", "c")}
    check(counts == {"d":1, "f":21, "c":100}, f"Unexpected instance size: {path}: {counts}")
    check(all(float(n[4]) >= 0 for n in nodes), "Delivery-only demand must be nonnegative")
    depot = next(n for n in nodes if n[1] == "d")
    return dict(counts=counts, parameters=params, horizon=float(depot[6])-float(depot[5]),
                sha256=sha(path))


def freeze(output):
    provenance = {}
    baseline = Path("artifacts/alns_p5/cache_20261020/ALNSAuditRunner_before")
    current = Path("build-audit-wsl-p6-bounds/ALNSAuditRunner")
    old = json.loads(Path("artifacts/alns_p6/insertion_bounds_20261021/experiment/manifest.json").read_text())
    check(old["status"] == "completed" and old["executable_sha256"]["current"] == HASHES["current"], "Incomplete P6 evidence")
    provenance["baseline"] = verify_source(REVISIONS["baseline"],
        Path("artifacts/alns_p5/profiling_20261020/experiment"), output/"source_baseline.zip")
    archive = subprocess.check_output(["git", "archive", "--format=zip", REVISIONS["current"], "--", "CMakeLists.txt", "include", "src"])
    (output/"source_current.zip").write_bytes(archive)
    checked = 0
    with zipfile.ZipFile(io.BytesIO(archive)) as files, zipfile.ZipFile(
            "artifacts/alns_p6/insertion_bounds_20261021/experiment/source_current.zip") as evidence:
        for name in files.namelist():
            relevant = name.startswith(("include/", "src/core/", "src/alns/", "src/io/", "src/utils/", "src/logger/", "src/app/")) or name in ("src/test/ALNSAuditRunner.cpp", "src/test/ScheduleValidator.h")
            if relevant and name.endswith((".cpp", ".h")):
                check(files.read(name).replace(b"\r\n", b"\n") == evidence.read(name).replace(b"\r\n", b"\n"), f"P6 source mismatch: {name}")
                check(files.read(name).replace(b"\r\n", b"\n") == Path(name).read_bytes().replace(b"\r\n", b"\n"), f"Working source differs: {name}")
                checked += 1
    provenance["current"] = dict(revision=subprocess.check_output(["git", "rev-parse", REVISIONS["current"]], text=True).strip(),
        verified_production_files=checked, archive_sha256=sha(output/"source_current.zip"),
        note="P6 was explicitly compiled ON; final Git commit changed the default to ON after the P6 audit.")
    binaries = {}
    for mode, source in (("baseline", baseline), ("current", current)):
        check(sha(source) == HASHES[mode], f"Unexpected binary: {source}")
        target = output/f"ALNSAuditRunner_{mode}"
        shutil.copy2(source, target)
        target.chmod(0o755)
        binaries[mode] = target
        provenance[mode]["executable_sha256"] = sha(target)
    for mode, folder in (("baseline", "build-audit-wsl-p5-profile"), ("current", "build-audit-wsl-p6-bounds")):
        shutil.copy2(Path(folder)/"CMakeFiles/alns_lib.dir/flags.make", output/f"{mode}_flags.make")
    flags = [(output/f"{mode}_flags.make").read_text() for mode in ("baseline", "current")]
    check(next(l for l in flags[0].splitlines() if l.startswith("CXX_FLAGS =")) ==
          next(l for l in flags[1].splitlines() if l.startswith("CXX_FLAGS =")), "Optimization flags differ")
    check("-DALNS_INSERTION_TIME_BOUNDS=1" in flags[1], "P6 must be ON")
    return binaries, provenance


def main(output, seconds=60, preflight_iterations=200):
    check(seconds > 0 and preflight_iterations > 0, "Positive budgets required")
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    binaries, provenance = freeze(output)
    data, metadata = {}, {}
    for case in CASES:
        data[case] = output/f"{case}.txt"
        shutil.copy2(Path("data/test-100")/f"{case}.txt", data[case])
        metadata[case] = instance_metadata(data[case])
    tools = [Path(__file__), Path(__file__).with_name("validate_audit_schedule.py"),
             Path(__file__).with_name("summarize_p7_rc_audit.py"),
             Path(__file__).with_name("summarize_charging_repair_audit.py"),
             Path(__file__).with_name("run_total_speed_audit.py")]
    tool_hashes = {str(p):sha(p) for p in tools}
    for path in tools:
        shutil.copy2(path, output/path.name)
    manifest = dict(status="running", cases=CASES, seeds=[1,2,3,4,5], seconds=seconds,
        max_iterations=25000, preflight_iterations=preflight_iterations, preflight_seeds=[1,2],
        revisions=REVISIONS, provenance=provenance, instances=metadata, tools_sha256=tool_hashes,
        configuration=dict(charging_repair=False, profiling=False, scatter_search=False,
            current_reconstruction_cache=2, current_selective_ejection=1, current_insertion_bounds=True,
            score_non_dominated=25, local_search_intensity=25, hv_stagnation_limit=20),
        environment=dict(platform=platform.platform(), compiler=subprocess.check_output(["c++", "--version"], text=True)),
        preflight=[], comparisons=[], runs=[])
    dump(output/"manifest.json", manifest)

    def execute(case, seed, mode, kind):
        count = preflight_iterations if kind == "preflight" else manifest["max_iterations"]
        budget = 3600 if kind == "preflight" else seconds
        folder = output/f"{kind}_{case}_s{seed}_{mode}"
        args = [str(binaries[mode]), str(data[case]), str(folder), str(budget), str(seed), str(count), "0", "0"]
        if mode == "current":
            args += ["2", "1"]
        cpu0 = resource.getrusage(resource.RUSAGE_CHILDREN)
        start = time.monotonic()
        with (output/f"{folder.name}.log").open("x") as log:
            subprocess.run(args, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=budget+600)
        elapsed = time.monotonic()-start
        cpu1 = resource.getrusage(resource.RUSAGE_CHILDREN)
        report = validate(data[case], folder)
        dump(folder/"independent_validation.json", report)
        check(report["solutions"] > 0 and report["invalid"] == 0, f"Invalid independent replay: {folder}")
        check(not (folder/"validation.txt").read_text().strip(), f"C++ validation: {folder}")
        stats = json.loads((folder/"run_statistics.json").read_text())
        check(stats["stop_reason"] in ("time_limit", "iteration_limit", "hypervolume_stagnation"), stats)
        if kind == "preflight":
            check(stats["iterations"] == count and stats["stop_reason"] == "iteration_limit", stats)
        config = json.loads((folder/"audit_parameters.json").read_text())
        check(config["max_time_ms"] == budget*1000 and config["max_iterations"] == count and
              config["seed"] == seed and config["charging_repair"] == 0 and config["profile_local_search"] == 0, config)
        if mode == "current":
            check(config["reconstruction_cache_mode"] == 2 and config["selective_ejection"] == 1, config)
            check(json.loads((folder/"insertion_time_bounds.json").read_text())["enabled"], "P6 not enabled")
        rows = list(csv.DictReader((folder/"results.csv").open()))
        best = min(rows, key=lambda r:(int(r["vehicles"]), float(r["distance"])))
        result = dict(case=case, seed=seed, mode=mode, kind=kind, output=folder.name, command=args,
            wall_seconds=elapsed, cpu_seconds=cpu1.ru_utime-cpu0.ru_utime+cpu1.ru_stime-cpu0.ru_stime,
            search_overshoot_ms=max(0, stats["search_ms"]-budget*1000),
            total_wall_excess_seconds=max(0, elapsed-budget), stats=stats, best=best,
            solutions=report["solutions"], routes=report["routes"], invalid=0,
            results_sha256=sha(folder/"results.csv"), schedules_sha256=sha(folder/"schedules.csv"))
        dump(folder/"process.json", result)
        print(f"{folder.name}: wall={elapsed:.2f}s iters={stats['iterations']} stop={stats['stop_reason']} "
              f"fleet={best['vehicles']} distance={float(best['distance']):.3f} valid={report['solutions']}", flush=True)
        return result

    # Fixed-iteration preflight also warms up both cases and binaries. It is not
    # pooled with time-budget quality comparisons or used as repeated timing.
    for case in CASES:
        for seed in manifest["preflight_seeds"]:
            rows = {}
            order = ["baseline", "current"] if (seed+CASES.index(case))%2 else ["current", "baseline"]
            for mode in order:
                row = execute(case, seed, mode, "preflight")
                rows[mode] = row
                manifest["preflight"].append(row)
                dump(output/"manifest.json", manifest)
            a, b = (output/rows[m]["output"] for m in ("baseline", "current"))
            check(objective_rows(a) == objective_rows(b) and sha(a/"schedules.csv") == sha(b/"schedules.csv"),
                  f"Fixed-iteration behavior changed: {case} seed {seed}")
            manifest["comparisons"].append(dict(case=case, seed=seed, identical_objectives=True, byte_identical_schedules=True))
            dump(output/"manifest.json", manifest)
    for seed in manifest["seeds"]:
        for case in CASES[(seed-1)%2:]+CASES[:(seed-1)%2]:
            order = ["baseline", "current"] if (seed+CASES.index(case))%2 else ["current", "baseline"]
            for position, mode in enumerate(order):
                row = execute(case, seed, mode, "quality")
                row.update(position=position, pair_order=order)
                manifest["runs"].append(row)
                dump(output/"manifest.json", manifest)
    check(all(sha(binaries[k]) == HASHES[k] for k in binaries), "Binary changed")
    check(all(sha(data[k]) == metadata[k]["sha256"] for k in data), "Input changed")
    check(tool_hashes == {str(p):sha(p) for p in tools}, "Audit tools changed")
    manifest["status"] = "completed"
    dump(output/"manifest.json", manifest)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--seconds", type=int, default=60)
    parser.add_argument("--preflight-iterations", type=int, default=200)
    args = parser.parse_args()
    main(args.output, args.seconds, args.preflight_iterations)
