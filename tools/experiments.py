"""Run isolated ALNS processes; only the parent writes the benchmark summary."""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import csv
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import shutil
import subprocess
import sys
import threading
import time

from .validate import check_run, read_instance

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_json(path, value):
    temp = path.with_name(path.name+".tmp")
    temp.write_text(json.dumps(value, indent=2, allow_nan=False)+"\n", encoding="utf-8")
    temp.replace(path)


def load_config(path):
    defaults = json.loads((ROOT/"configs/default.json").read_text(encoding="utf-8"))
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError("Duplicate parameter: "+key)
            result[key] = value
        return result
    supplied = json.loads(Path(path).read_text(encoding="utf-8-sig"), object_pairs_hook=unique)
    if not isinstance(supplied, dict):
        raise ValueError("Configuration must be a JSON object")
    unknown = set(supplied)-set(defaults)
    if unknown:
        raise ValueError("Unknown parameters: "+", ".join(sorted(unknown)))
    config = defaults | supplied
    for key, value in config.items():
        default = defaults[key]
        if isinstance(default, bool):
            good = type(value) is bool
        elif isinstance(default, int):
            good = type(value) is int
        else:
            good = type(value) in (int, float) and math.isfinite(value)
        if not good:
            raise ValueError("Invalid parameter type/value: "+key)
        if not isinstance(value, bool) and value < 0:
            raise ValueError("Negative parameter: "+key)
    for key in ("maxIterations", "maxTime", "segmentIterations", "hvStagnationLimit",
                "startTemperature", "minTemperature", "SAScale"):
        if config[key] <= 0:
            raise ValueError("Positive parameter required: "+key)
    if not 0 <= config["minRemoval"] <= config["maxRemoval"] <= 1:
        raise ValueError("Require 0 <= minRemoval <= maxRemoval <= 1")
    if not 0 <= config["decayParameter"] <= 1 or not 0 < config["coolingRate"] <= 1:
        raise ValueError("Invalid decay/cooling rate")
    if config["minTemperature"] > config["startTemperature"]:
        raise ValueError("minTemperature exceeds startTemperature")
    if config["localSearchIntensity"] > 100:
        raise ValueError("localSearchIntensity must be in [0,100]")
    for key in ("reconstructionCacheMode", "orOptCacheMode"):
        if config[key] not in (0, 1, 2):
            raise ValueError(key+" must be 0, 1 or 2")
    for key, value in config.items():
        if type(value) is int and value > (2**63-1 if key=="maxTime" else 2**31-1):
            raise ValueError("Integer parameter out of range: "+key)
    return config


def preflight(binary, sources, config_path):
    binary = Path(binary).resolve(strict=True)
    info = subprocess.run([str(binary), "--version"], capture_output=True, text=True, check=True)
    version = json.loads(info.stdout)
    if version.get("interface_version") != 1:
        raise ValueError("Wrong solver executable; build target evrptw")
    expected = json.loads((ROOT/"configs/variant.json").read_text())["variant"]
    if version.get("variant") != expected:
        raise ValueError("Executable belongs to a different variant; rebuild evrptw for this branch")
    config = load_config(config_path)
    files = []
    names = set()
    for source in sources:
        path = Path(source).resolve(strict=True)
        if not path.is_file():
            raise ValueError("Not an instance file: "+str(path))
        if path.stem.casefold() in names:
            raise ValueError("Instance names collide in output: "+path.stem)
        names.add(path.stem.casefold())
        read_instance(path)  # Fail before scheduling for unsupported/malformed input.
        files.append((path, path.read_bytes()))
    if not files:
        raise ValueError("No instance files matched")
    provenance = dict(binary=str(binary), binary_sha256=digest(binary), solver=version,
                      configuration=config, configuration_source=str(Path(config_path).resolve()))
    return binary, files, config, provenance


def run_one(binary, source, data, config, seed, output, provenance, stop, active, lock):
    output.mkdir(parents=True, exist_ok=False)
    (output/"input.txt").write_bytes(data)
    write_json(output/"parameters.json", config)
    (output/"parameters.cfg").write_text("".join(
        f"{key}={int(value) if type(value) is bool else value}\n" for key, value in sorted(config.items())), encoding="utf-8")
    metadata = dict(provenance, instance=str(source), instance_name=source.stem,
                    input_sha256=hashlib.sha256(data).hexdigest(), seed=seed,
                    started_utc=datetime.now(timezone.utc).isoformat(), status="running")
    command = [str(binary), str(output/"input.txt"), str(output/"parameters.cfg"), str(output), str(seed)]
    metadata["command"] = command
    write_json(output/"run.json", metadata)
    row = dict(instance=source.stem, seed=seed, status="error", vehicles=None, distance=None,
               gini=None, max_time=None, solution=None, front_size=0, routes=0, invalid=None,
               iterations=None, stop_reason=None, solver_ms=None, wall_ms=None, output=str(output))
    start = time.monotonic()
    try:
        if stop.is_set():
            raise RuntimeError("Cancelled before launch")
        with (output/"stdout.log").open("wb") as stdout, (output/"stderr.log").open("wb") as stderr:
            with lock:
                if stop.is_set():
                    raise RuntimeError("Cancelled before launch")
                proc = subprocess.Popen(command, stdout=stdout, stderr=stderr)
                active.add(proc)
            try:
                exit_code = proc.wait()
            finally:
                with lock:
                    active.discard(proc)
        metadata["solver_exit_code"] = exit_code
        report = check_run(output)
        row.update(front_size=report["solutions"], routes=report["routes"], invalid=report["invalid"])
        if exit_code != 0:
            raise RuntimeError("Solver failed, exit="+str(exit_code)+"; see stderr.log")
        if not report["valid"]:
            raise RuntimeError("Independent validation failed; see validation.json")
        stats = json.loads((output/"run_statistics.json").read_text())
        row.update(iterations=stats["iterations"], stop_reason=stats["stop_reason"], solver_ms=stats["total_ms"])
        with (output/"results.csv").open() as handle:
            candidates = list(csv.DictReader(handle))
        best = min(candidates, key=lambda item:(int(item["vehicles"]), float(item["distance"])))
        row.update(vehicles=int(best["vehicles"]), distance=float(best["distance"]),
                   gini=float(best["gini"]), max_time=float(best["max_time"]),
                   solution=int(best["solution"]), status="ok")
        write_json(output/"representative.json", dict(selection="minimum vehicles, then minimum distance", **best))
    except Exception as error:
        row["status"] = "failed"
        metadata["error"] = str(error)
        (output/"runner_error.txt").write_text(str(error)+"\n", encoding="utf-8")
        if not (output/"validation.json").exists():
            check_run(output)
    row["wall_ms"] = round((time.monotonic()-start)*1000)
    metadata.update(status=row["status"], finished_utc=datetime.now(timezone.utc).isoformat(), summary=row)
    write_json(output/"run.json", metadata)
    return row


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_subparsers(dest="mode", required=True)
    for name in ("single", "benchmark"):
        sub = modes.add_parser(name)
        sub.add_argument("--binary", type=Path, default=ROOT/"build-release"/("evrptw.exe" if sys.platform=="win32" else "evrptw"))
        sub.add_argument("--config", type=Path, default=ROOT/"configs/default.json")
        sub.add_argument("--output", type=Path, required=True)
        if name=="single":
            sub.add_argument("--instance", type=Path, required=True)
            sub.add_argument("--seed", type=int, default=1)
        else:
            sub.add_argument("--instances-dir", type=Path, required=True)
            sub.add_argument("--pattern", default="*.txt")
            sub.add_argument("--workers", type=int, required=True)
    args = parser.parse_args(argv)
    stop, lock, active = threading.Event(), threading.Lock(), set()
    pool = None
    manifest = None
    try:
        if args.mode=="single":
            if not 1 <= args.seed <= 2**32-1:
                raise ValueError("Seed must be in [1, 4294967295]")
            sources, workers = [args.instance], 1
        else:
            if not args.instances_dir.is_dir() or args.workers <= 0:
                raise ValueError("Valid instances directory and positive workers required")
            if "/" in args.pattern or "\\" in args.pattern:
                raise ValueError("Pattern must select files directly within the given folder")
            sources, workers = sorted(args.instances_dir.glob(args.pattern)), args.workers
        output = args.output.resolve()
        if output.exists():
            raise ValueError("Refusing to overwrite output: "+str(output))
        binary, files, config, provenance = preflight(args.binary, sources, args.config)
        if args.mode=="benchmark":
            output.mkdir(parents=True, exist_ok=False)
        jobs = [(source, data, seed, output/source.stem/f"seed_{seed}")
                for source, data in files for seed in range(1,11)] if args.mode=="benchmark" else [(*files[0], args.seed, output)]
        manifest = dict(provenance, mode=args.mode, workers=workers, jobs=len(jobs), completed=0, status="running")
        if args.mode=="benchmark":
            write_json(output/"benchmark.json", manifest)
        pool = ThreadPoolExecutor(max_workers=workers)
        futures = [pool.submit(run_one, binary, source, data, config, seed, folder, provenance, stop, active, lock)
                   for source, data, seed, folder in jobs]
        rows = []
        for future in as_completed(futures):
            row = future.result()
            rows.append(row)
            print(f"[{len(rows)}/{len(jobs)}] {row['instance']} seed_{row['seed']}: {row['status']}"
                  f" vehicles={row['vehicles']} distance={row['distance']}", flush=True)
            if args.mode=="benchmark":
                ordered = sorted(rows, key=lambda item:(item["instance"], item["seed"]))
                temp = output/"summary.csv.tmp"
                with temp.open("w", newline="", encoding="utf-8") as handle:
                    writer = csv.DictWriter(handle, fieldnames=list(row))
                    writer.writeheader(); writer.writerows(ordered)
                temp.replace(output/"summary.csv")
                manifest.update(completed=len(rows), failed=sum(item["status"]!="ok" for item in rows))
                write_json(output/"benchmark.json", manifest)
        passed = all(row["status"]=="ok" for row in rows)
        if args.mode=="benchmark":
            manifest["status"] = "passed" if passed else "failed"
            write_json(output/"benchmark.json", manifest)
        return 0 if passed else 1
    except KeyboardInterrupt:
        stop.set()
        with lock:
            for proc in active:
                proc.terminate()
        if manifest is not None and args.mode=="benchmark":
            manifest["status"]="cancelled"
            write_json(output/"benchmark.json",manifest)
        print("Cancelled; completed run directories and logs are retained.", file=sys.stderr)
        return 130
    except Exception as error:
        print("ERROR: "+str(error), file=sys.stderr)
        return 1
    finally:
        if pool:
            pool.shutdown(wait=True, cancel_futures=stop.is_set())
