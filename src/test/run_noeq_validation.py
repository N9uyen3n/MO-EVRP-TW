"""Fixed-iteration NoEQ scope audit plus restoration check against frozen first.

Run from the repository root under the same WSL Release environment as the builds.
All output directories must be new. This is a correctness audit, not a quality study.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import subprocess

from validate_audit_schedule import validate


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--noeq", type=Path, required=True)
    parser.add_argument("--full", type=Path, required=True)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--iterations", type=int, default=2000)
    parser.add_argument("--reference-cases", nargs="+", choices=("r101", "rc205"), default=["r101"],
                        help="restoration checks; rc205 is optional and can be expensive")
    args = parser.parse_args()
    if args.iterations <= 0:
        parser.error("positive iterations required")
    args.output.mkdir(parents=True, exist_ok=False)
    report = dict(iterations=args.iterations, seed=1, runs=[], restoration_checks=[], binaries={})
    for name in ("noeq", "full", "baseline"):
        binary = getattr(args, name).resolve(strict=True)
        setattr(args, name, binary)
        report["binaries"][name] = dict(path=str(binary), sha256=hashlib.sha256(binary.read_bytes()).hexdigest())

    def save():
        (args.output / "validation_report.json").write_text(json.dumps(report, indent=2)+"\n")

    def run(case, variant):
        source = Path("data/solomon") / (case+"_21.txt")
        output = args.output / (case+"_"+variant)
        command = [str(getattr(args, variant)), str(source), str(output), "3600", "1", str(args.iterations)]
        with (args.output / (case+"_"+variant+".log")).open("x") as log:
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
        replay = validate(source, output)
        (output / "independent_validation.json").write_text(json.dumps(replay, indent=2)+"\n")
        assert replay["solutions"] and replay["invalid"] == 0, replay
        stats = json.loads((output / "run_statistics.json").read_text())
        assert stats["iterations"] == args.iterations and stats["stop_reason"] == "iteration_limit", stats
        parameters = json.loads((output / "audit_parameters.json").read_text())
        if variant != "baseline":
            assert parameters["variant"] == ("FP-PALNS-NoEQ" if variant == "noeq" else "FP-PALNS")
            assert parameters["equity_search"] == (variant == "full")
        report["runs"].append(dict(case=case, variant=variant, solutions=replay["solutions"],
                                   routes=replay["routes"], invalid=replay["invalid"], statistics=stats,
                                   output=str(output), instance_sha256=replay["instance_sha256"], parameters=parameters))
        save()
        print(f"PASS {case} {variant}: {replay['solutions']} solutions / {replay['routes']} routes", flush=True)
        return output

    def objectives(output):
        with (output / "results.csv").open() as handle:
            return [{k:v for k,v in row.items() if k != "elapsed_ms"} for row in csv.DictReader(handle)]

    def config(output):
        rows = (output / "audit_config.txt").read_text().splitlines()
        ignored = {"RunPrefix", "Timestamp", "variant", "searchArchiveObjectives", "equitySearchEnabled"}
        return {key.strip(): value.strip() for row in rows if ":" in row
                for key, value in [row.split(":", 1)] if key.strip() not in ignored}

    noeq_outputs = {case:run(case, "noeq") for case in ("r101", "r104", "c109", "rc106", "rc205")}
    for case in args.reference_cases:
        baseline, full = run(case, "baseline"), run(case, "full")
        assert objectives(baseline) == objectives(full), f"{case}: full mode changes baseline objectives"
        assert (baseline / "schedules.csv").read_bytes() == (full / "schedules.csv").read_bytes(), f"{case}: schedule mismatch"
        assert config(full) == config(noeq_outputs[case]), f"{case}: inherited numerical configuration mismatch"
        report["restoration_checks"].append(dict(case=case, same_objectives=True,
            identical_exported_schedules=True, same_full_noeq_numerical_configuration=True))
        save()
        print(f"PASS {case}: full mode restores frozen baseline; NoEQ inherits numerical parameters", flush=True)
    report["status"] = "passed"
    save()


if __name__ == "__main__":
    main()
