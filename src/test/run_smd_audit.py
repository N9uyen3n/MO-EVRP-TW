"""Paired SMD audits using immutable stages from freeze_smd_stage.py (WSL)."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import resource
import shutil
import subprocess
import time

from freeze_smd_stage import CASES, sha
from validate_audit_schedule import validate


def require(ok, message):
    if not ok:
        raise RuntimeError(str(message))


def dump(path, value):
    temporary = path.with_suffix(path.suffix+".tmp")
    temporary.write_text(json.dumps(value, indent=2)+"\n", encoding="utf-8")
    temporary.replace(path)


def objectives(folder):
    with (folder/"results.csv").open(newline="") as stream:
        return [{k: v for k, v in row.items() if k != "elapsed_ms"} for row in csv.DictReader(stream)]


def run(args):
    require(args.iterations > 0 and args.repetitions > 0 and args.seconds > 0, "Positive budgets required")
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=False)
    stages = {}
    for mode, source in (("before", args.before), ("after", args.after)):
        source = source.resolve()
        metadata = json.loads((source/"stage.json").read_text())
        for name, digest in metadata["files"].items():
            require(sha(source/name) == digest, f"Frozen stage changed: {source/name}")
        target = root/mode
        shutil.copytree(source, target)
        (target/"ALNSAuditRunner").chmod(0o755)
        stages[mode] = metadata
    for case in args.cases:
        require(case in CASES, f"Unsupported audit case {case}")
        require(sha(root/"before"/f"{case}.txt") == sha(root/"after"/f"{case}.txt"), "Input mismatch")
    tool_names = (Path(__file__).name, "freeze_smd_stage.py", "validate_audit_schedule.py")
    for name in tool_names:
        shutil.copy2(Path(__file__).with_name(name), root/name)
    manifest = dict(status="running", kind=args.kind, cases=args.cases, seeds=args.seeds,
                    iterations=args.iterations, seconds=args.seconds, repetitions=args.repetitions,
                    stages=stages, mode_settings={"before": args.before_mode, "after": args.after_mode},
                    profiling=args.profile, warmup=args.warmup,
                    tool_sha256={name: sha(root/name) for name in tool_names},
                    warmups=[], runs=[], pairs=[], started_unix=time.time())
    dump(root/"manifest.json", manifest)

    def execute(case, seed, mode, name, iterations):
        folder = root/name
        command = [str(root/mode/"ALNSAuditRunner"), str(root/mode/f"{case}.txt"), str(folder),
                   str(args.seconds), str(seed), str(iterations), "0", str(args.profile), "2", "1",
                   str(manifest["mode_settings"][mode])]
        start_cpu = resource.getrusage(resource.RUSAGE_CHILDREN)
        start = time.monotonic()
        with (root/f"{name}.log").open("x") as log:
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=args.seconds+300)
        wall = time.monotonic()-start
        end_cpu = resource.getrusage(resource.RUSAGE_CHILDREN)
        report = validate(root/mode/f"{case}.txt", folder)
        dump(folder/"independent_validation.json", report)
        require(report["solutions"] > 0 and report["invalid"] == 0 and report["errors"] == 0,
                f"Independent validation failed: {folder}")
        require(not (folder/"validation.txt").read_text().strip(), f"C++ validation failed: {folder}")
        config = json.loads((folder/"audit_parameters.json").read_text())
        require(config["seed"] == seed and config["max_iterations"] == iterations and
                config["max_time_ms"] == args.seconds*1000 and config["charging_repair"] == 0 and
                config["profile_local_search"] == args.profile and config["reconstruction_cache_mode"] == 2 and
                config["selective_ejection"] == 1 and config.get("or_opt_cache_mode", 0) == manifest["mode_settings"][mode], config)
        stats = json.loads((folder/"run_statistics.json").read_text())
        if args.kind != "quality" or iterations != args.iterations:
            require(stats["iterations"] == iterations and stats["stop_reason"] == "iteration_limit", stats)
        else:
            require(stats["stop_reason"] == "time_limit", stats)
        rows = objectives(folder)
        best = min(rows, key=lambda row: tuple(float(row[k]) for k in ("vehicles", "distance", "gini", "max_time")))
        record = dict(case=case, seed=seed, mode=mode, output=name, wall_seconds=wall,
                      cpu_seconds=end_cpu.ru_utime+end_cpu.ru_stime-start_cpu.ru_utime-start_cpu.ru_stime,
                      solutions=report["solutions"], routes=report["routes"], invalid=report["invalid"],
                      best=best, stats=stats, csv_sha256={n: sha(folder/n) for n in ("results.csv", "schedules.csv")},
                      validation_sha256=sha(folder/"independent_validation.json"))
        if (folder/"or_opt_cache.json").exists():
            record["or_opt_cache"] = json.loads((folder/"or_opt_cache.json").read_text())
        return record

    try:
        if args.warmup:
            for mode in ("before", "after"):
                record = execute(args.cases[0], 123, mode, f"warmup_{mode}", 25)
                manifest["warmups"].append(record)
                dump(root/"manifest.json", manifest)
        for repetition in range(1, args.repetitions+1):
            for offset in range(len(args.cases)):
                case_index = (offset+repetition-1) % len(args.cases)
                case = args.cases[case_index]
                for seed in args.seeds:
                    order = ["before", "after"] if (case_index+seed+repetition)%2 == 0 else ["after", "before"]
                    pair = dict(case=case, seed=seed, repetition=repetition, order=order)
                    selected = {}
                    for mode in order:
                        name = f"{case}_s{seed}_rep{repetition}_{mode}"
                        record = execute(case, seed, mode, name, args.iterations)
                        record.update(repetition=repetition, pair_order=order)
                        manifest["runs"].append(record)
                        selected[mode] = record
                        dump(root/"manifest.json", manifest)
                        print(f"{len(manifest['runs'])}/{len(args.cases)*len(args.seeds)*args.repetitions*2} {name}: "
                              f"wall={record['wall_seconds']:.3f}s valid={record['solutions']} iterations={record['stats']['iterations']}", flush=True)
                    before, after = (root/selected[mode]["output"] for mode in ("before", "after"))
                    pair.update(identical_objectives=objectives(before) == objectives(after),
                                byte_identical_schedules=sha(before/"schedules.csv") == sha(after/"schedules.csv"),
                                reduction_percent=100*(1-selected["after"]["wall_seconds"]/selected["before"]["wall_seconds"]))
                    if args.kind in ("equivalence", "speed"):
                        require(pair["identical_objectives"] and pair["byte_identical_schedules"], f"Search changed: {pair}")
                    manifest["pairs"].append(pair)
                    dump(root/"manifest.json", manifest)
        # Verify the raw evidence and frozen stages again after all runs.
        for mode in ("before", "after"):
            for name, digest in stages[mode]["files"].items():
                require(sha(root/mode/name) == digest, f"Stage changed during experiment: {mode}/{name}")
        for record in manifest["runs"]+manifest["warmups"]:
            for name, digest in record["csv_sha256"].items():
                require(sha(root/record["output"]/name) == digest, "CSV changed during experiment")
        manifest.update(status="completed", completed_unix=time.time(),
                        solutions=sum(r["solutions"] for r in manifest["runs"]),
                        routes=sum(r["routes"] for r in manifest["runs"]))
        dump(root/"manifest.json", manifest)
        print(f"COMPLETED {len(manifest['runs'])} runs; {manifest['solutions']} solutions / {manifest['routes']} routes valid", flush=True)
    except BaseException as error:
        manifest.update(status="failed", error=str(error))
        dump(root/"manifest.json", manifest)
        raise


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", type=Path, required=True)
    parser.add_argument("--after", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--kind", choices=("equivalence", "speed", "quality", "observe", "scope"), default="equivalence")
    parser.add_argument("--cases", nargs="+", default=list(CASES))
    parser.add_argument("--seeds", type=int, nargs="+", default=[1, 2])
    parser.add_argument("--iterations", type=int, default=200)
    parser.add_argument("--seconds", type=int, default=3600)
    parser.add_argument("--repetitions", type=int, default=1)
    parser.add_argument("--before-mode", type=int, choices=(0, 1, 2), default=0)
    parser.add_argument("--after-mode", type=int, choices=(0, 1, 2), default=0)
    parser.add_argument("--profile", type=int, choices=(0, 1), default=0)
    parser.add_argument("--warmup", action="store_true")
    run(parser.parse_args())
