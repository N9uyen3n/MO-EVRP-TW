"""Paired, sequential ALNS audit runs; each output directory must be new."""
import csv
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CASES = ["data/test1/c101C5.txt", "data/test1/r102C10.txt", "data/test1/rc108C10.txt"]


def main():
    executable = Path(sys.argv[1]).resolve()
    output = Path(sys.argv[2]).resolve()
    seconds = int(sys.argv[3]) if len(sys.argv) > 3 else 10
    output.mkdir(parents=True, exist_ok=True)
    summaries = []
    for source in CASES:
        for seed in (1, 2):
            name = f"{Path(source).stem}_seed{seed}"
            directory = output / name
            if directory.exists():
                raise RuntimeError(f"Refusing to overwrite {directory}")
            directory.mkdir()
            command = [str(executable), str(ROOT / source), str(directory), str(seconds), str(seed), "25000"]
            with (directory / "console.txt").open("w", encoding="utf-8") as console:
                result = subprocess.run(command, cwd=ROOT, stdout=console, stderr=subprocess.STDOUT, timeout=120)
            rows = list(csv.DictReader((directory / "results.csv").open())) if (directory / "results.csv").exists() else []
            best = min(rows, key=lambda r: (int(r["vehicles"]), float(r["distance"]))) if rows else {}
            summary = dict(instance=source, seed=seed, seconds=seconds, max_iterations=25000,
                           exit_code=result.returncode, front=len(rows),
                           invalid=sum(r["valid"] != "1" for r in rows), best=best)
            summaries.append(summary)
            (output / "summary.json").write_text(json.dumps(summaries, indent=2), encoding="utf-8")
            print(json.dumps(summary), flush=True)


if __name__ == "__main__":
    main()
