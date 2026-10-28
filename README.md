# EVRPTW-PR experiments

## Tài liệu tiếng Việt

- [Hướng dẫn đầy đủ: build, single, benchmark một file/cả thư mục, số lần chạy, seed, workers và kiểm định](docs/HUONG_DAN_CHAY.md)
- [Toàn bộ tham số: mặc định, đơn vị, miền giá trị và cách override JSON](docs/THAM_SO.md)
- [Chi tiết mã nguồn: luồng xử lý, mô hình bài toán, ALNS, toán tử, local search và Full/NoEQ](docs/SOURCE_CODE.md)

Delivery-only EVRPTW with partial recharging. Both branches use the same commands:
`first` builds FP-PALNS; `first-noEq` builds FP-PALNS-NoEQ. NoEQ excludes Gini
from search decisions and retains its true value in the results. JD input is
outside this setup; input must use the existing Solomon EVRPTW text format,
including vehicle/charging parameters `Q`, `C`, `r`, `g`, `v`.

## Build once

Requires CMake 3.17+, a C++17 compiler and Python 3.10+. No Python packages needed.
Run the following from the repository root, either in WSL/Linux or a configured
Windows compiler environment:

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --target evrptw -j4
```

Use a separate build directory for each checkout/branch. The default build
uses the branch's fixed variant; rebuilding after a branch switch also updates
an old CMake cache. The launcher refuses an executable from the other variant.
The default build creates only the experiment executable and its library.
Regression tests are optional (`-DBUILD_TESTING=ON`). Historical drivers,
tuning programs, audit artifacts and duplicate datasets have been removed from
these branches. No generated executables or experiment logs are tracked.

The supported dataset lives in `data/solomon/`; it contains both full instances
(`*_21.txt`) and small instances (`*C5.txt`, `*C10.txt`, `*C15.txt`). To benchmark
only full instances, add `--pattern '*_21.txt'`. Current source dependencies are
listed explicitly in CMake. Previous versions remain available in Git history.

## 1. One instance

```sh
python run.py single --instance data/solomon/r101_21.txt --config configs/default.json --seed 1 --output results/r101_21/seed_1
```

Use `python3` if that is your Python command. On Windows the executable suffix
is detected automatically; multi-configuration generators may require
`--binary build-release/Release/evrptw.exe`. For a different build directory,
pass `--binary /path/to/evrptw`.

## 2. Every instance in one directory, seeds 1 through 10

```sh
python run.py benchmark --instances-dir data/solomon --config configs/default.json --workers 4 --output results/benchmark
```

This selects `*.txt` directly inside the given directory. Override `--pattern`
if needed. `--workers n` runs at most `n` independent solver processes, each with
its own random generator, seed and output files. It does not share one ALNS
search across threads. Every selected instance defaults to seeds **1–10**.
Use `--runs N --seed-start S` to run N consecutive seeds starting at S.
To benchmark one file repeatedly, replace `--instances-dir data/solomon` with
`--instance data/solomon/r101_21.txt`.

```text
results/benchmark/
  benchmark.json
  summary.csv
  r101_21/
    seed_1/
    seed_2/
    ...
    seed_10/
  r104_21/
    seed_1/
    ...
    seed_10/
```

`summary.csv` is updated after each completed job, ordered by instance/seed.
Its representative is the valid solution with minimum vehicles, then minimum
distance. All returned archive solutions remain in each run's results.
The launcher returns nonzero if any job fails validation or execution. Ctrl+C
terminates active solver processes and retains logs already written. Existing
output directories are refused; choose a new output directory for each batch.

## Parameters

Edit or copy `configs/default.json`. A custom JSON file may contain only the
values you want to override; all other values inherit the checked-in defaults.
Unknown/duplicate keys and invalid types/ranges are errors, never silently
ignored. Seed belongs to the command (single) or benchmark seed schedule.

`maxTime` is **milliseconds of ALNS search time**, not a hard process timeout.
Initialization occurs before it, a running neighborhood can overrun it, and
optional Scatter Search has its own configured budget. Logs record actual
initialization/search/total durations and the stop reason. `maxIterations` and
HV convergence can also end a run. `configs/smoke.json` is a short functional
test configuration, not a calibrated benchmark configuration.

Common numerical defaults retain the saved TestALNS values (LS intensity 15,
non-dominated reward 20); neither branch is independently recalibrated.
Logging/profiling is always enabled in this interface. Every ALNS iteration,
including infeasible candidates, is logged; this I/O is included in measured
runtime. Insertion/cache/charging/LS diagnostics are recorded even when a feature
is disabled, so zero activity is distinguishable from a missing report.

## Files inside every seed directory

| Files | Content |
|---|---|
| `input.txt`, `parameters.json`, `parameters.cfg`, `run.json` | Input snapshot, fully resolved parameters, exact command, seed, variant, binary/input hashes, status and timestamps |
| `stdout.log`, `stderr.log` | Solver console output and errors |
| `search_iterations.csv`, `search_evolution_log.txt` | Every main ALNS iteration: selected operators, outcome, objectives and detailed candidate solution |
| `search_progress.csv`, `search_operators.csv` | Archive progress and adaptive operator weights/scores/usages per segment, including the last partial segment |
| `search_config.txt`, `search_summary.txt`, `search_front_*` | Solver configuration, summary and complete final archive logs |
| `results.csv`, `schedules.csv` | All four true objectives and full-precision actual visit states for every returned solution |
| `run_statistics.json`, `local_search_profile.json`, `*_cache.json`, `ejection_undo.json`, `insertion_time_bounds.json` | Stop reason, timing, charging-repair counters and available performance diagnostics |
| `cpp_validation.txt` | C++ independent replay diagnostics; empty means no detected error |
| `validation.json`, `validation.txt` | Independent Python replay verdict and detailed violations |
| `representative.json` | Chosen representative and the selection rule |
| `runner_error.txt` | Failure description, when a run fails |

## Recheck a saved solution independently

```sh
python tools/validate.py --run results/benchmark/r101_21/seed_1
```

This uses the saved input and actual exported schedules; it does **not** repair
routes, change charging decisions, or reschedule visits. It checks customer
coverage exactly once, depot endpoints, delivery capacity, battery before and
after charging, charging duration, service/waiting, time windows/station closing,
and all four reported objectives. Python validation is independent of the C++
feasibility flags. A feasible result is not a certificate of optimality.

## Quick verification of this setup

Both variants were built in WSL Release and passed the functional checks:
single-run artifacts, two small instances × seeds 1–10 with three parallel
processes, sequential/parallel consistency, overwrite protection, configuration
validation, initialization failure, and rejection of tampered battery/objectives,
missing customers, duplicate solution IDs and changed input snapshots.
Both also passed C++ and Python replay on a short 20-iteration `r101_21` run
with `configs/smoke.json`. This is interface/correctness validation, not a long
quality benchmark. To repeat the functional tests:

```sh
python -m tools.test_experiments --binary build-release/evrptw
```
