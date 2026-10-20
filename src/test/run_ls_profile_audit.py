"""WSL/Linux: fixed-iteration baseline/off/on audit, sequential frozen binaries."""
import csv
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time

from validate_audit_schedule import validate


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def dump(path, value):
    path.write_text(json.dumps(value, indent=2)+"\n", encoding="utf-8")


def run(before, after, root, iterations=200):
    root = root.resolve()
    # The caller supplies an existing parent containing only the preserved baseline.
    target = root/"experiment"
    target.mkdir(parents=False, exist_ok=False)
    binaries = {}
    for mode, source in (("before", before), ("after", after)):
        executable = target/f"ALNSAuditRunner_{mode}"
        shutil.copy2(source, executable)
        executable.chmod(0o755)
        binaries[mode] = executable
    sources = [Path("CMakeLists.txt")]
    sources += sorted(p for base in ("include", "src") for p in Path(base).rglob("*")
                      if p.suffix in (".h", ".cpp", ".py") and "__pycache__" not in p.parts)
    hashes = {str(p): digest(p) for p in sources}
    for p in sources:
        destination = target/"source"/p
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(p, destination)
    manifest = dict(status="running", iterations=iterations, seeds=[1, 2],
                    instances=["r104_21", "r101_21", "c109_21"], charging_repair=False,
                    time_budget_seconds=3600, source_sha256=hashes,
                    git_head=subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip(),
                    executable_sha256={m:digest(p) for m,p in binaries.items()}, runs=[], comparisons=[])
    dump(target/"manifest.json", manifest)
    for case in manifest["instances"]:
        original = Path("data/test-100")/f"{case}.txt"
        data = target/f"{case}.txt"
        shutil.copy2(original, data)
        for seed in manifest["seeds"]:
            # Alternate measurement order; baseline verifies the old executable too.
            modes = ["before"]+(["off", "on"] if seed == 1 else ["on", "off"])
            outputs = {}
            for mode in modes:
                out = target/f"{case}_s{seed}_{mode}"
                binary = binaries["before" if mode == "before" else "after"]
                args = [str(binary), str(data), str(out), "3600", str(seed), str(iterations), "0"]
                if mode != "before":
                    args.append(str(int(mode == "on")))
                    args.append("0")  # Reproduce profiling-only audit without reconstruction cache.
                start = time.monotonic()
                with (target/f"{out.name}.log").open("x") as log:
                    subprocess.run(args, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=3700)
                elapsed = time.monotonic()-start
                validation = validate(data, out)
                dump(out/"independent_validation.json", validation)
                assert validation["solutions"] and validation["invalid"] == 0, out
                stats = json.loads((out/"run_statistics.json").read_text())
                assert stats["iterations"] == iterations and stats["stop_reason"] == "iteration_limit", stats
                if mode != "before":
                    profile = json.loads((out/"local_search_profile.json").read_text())
                    entries = profile["metrics"]
                    assert profile["enabled"] == (mode == "on")
                    assert all(0 <= e["self_ns"] <= e["inclusive_ns"] for e in entries.values())
                    if mode == "on":
                        # All profile scopes are below these two external entry points.
                        roots = entries["run"]["inclusive_ns"]+entries["intensified_reduction"]["inclusive_ns"]
                        assert roots == sum(e["self_ns"] for e in entries.values()), out
                    else:
                        assert all(e["calls"] == e["inclusive_ns"] == 0 for e in entries.values())
                entry = dict(case=case, seed=seed, mode=mode, wall_seconds=elapsed,
                             input_sha256=digest(data), stats=stats,
                             solutions=validation["solutions"], routes=validation["routes"], invalid=0)
                manifest["runs"].append(entry)
                outputs[mode] = out
                dump(target/"manifest.json", manifest)
                print(f"{out.name}: {elapsed:.3f}s, {validation['solutions']} valid solutions", flush=True)
            def objectives(out):
                with (out/"results.csv").open(newline="") as handle:
                    return [{k:v for k,v in row.items() if k != "elapsed_ms"} for row in csv.DictReader(handle)]
            same_objectives = objectives(outputs["before"]) == objectives(outputs["off"]) == objectives(outputs["on"])
            same_schedules = len({digest(out/"schedules.csv") for out in outputs.values()}) == 1
            assert same_objectives and same_schedules, (case, seed, "profiling changed search")
            manifest["comparisons"].append(dict(case=case, seed=seed, identical_objectives=True,
                                                  byte_identical_schedules=True))
            dump(target/"manifest.json", manifest)
    assert hashes == {str(p):digest(p) for p in sources}, "Sources changed during audit"
    assert manifest["executable_sha256"] == {m:digest(p) for m,p in binaries.items()}
    manifest["status"] = "completed"
    dump(target/"manifest.json", manifest)


if __name__ == "__main__":
    run(*map(Path, sys.argv[1:4]), iterations=int(sys.argv[4]) if len(sys.argv)>4 else 200)
