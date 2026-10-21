# FP-PALNS-NoEQ — branch `first-noEq`

Base: `first` at `0bafe274d75520ac7802e1f20be10a43ef684e7f`.
Delivery-only EVRPTW with partial recharging; feasibility, charging, distance
matrix assumptions, local search and all previous correctness fixes are inherited.

## Table 5. Search-active differences and common elements

| Component | FP-PALNS | FP-PALNS-NoEQ |
|---|---|---|
| Computed and reported objectives | (Z1, Z2, Z3, Z4) | (Z1, Z2, Z3, Z4) |
| Search archive at fixed fleet size | (Z2, Z3, Z4) | (Z2, Z4) |
| Gini in SA acceptance | Active | Disabled |
| Equity-oriented weight channels | Active | Disabled |
| Equity-aware operator rewards | Active | Disabled |
| Equity-aware perturbation | Active | Disabled |
| Ex-post Gini calculation | Retained | Retained |
| Common parameters | FP-PALNS-calibrated | Same inherited values |
| Independent recalibration | Not applicable | No |

Z1 = vehicles, Z2 = distance, Z3 = active-workload Gini, Z4 = MaxTime.
Z1 keeps the original strict priority. At equal Z1, NoEQ uses only Z2 and Z4.
Gini uses route active time (travel + service + charging, excluding waiting).
The real `Solution` metrics, four-objective `Solution::dominates`, exports and
independent schedule validators retain their original meanings.

## Exact ablation rules

- `include/alns/SearchObjectives.h` centralizes the search projection. Search
  dominance uses the inherited distance/MaxTime tolerances (1e-4), without Gini.
- Archive dominance, duplicate detection and crowding omit Gini. Equal distance
  and MaxTime are duplicates even when Gini differs. No Gini tie-break is used.
- Main-loop SA, segment restart selection and the optional Scatter Search
  mini-loop read projected search Gini = 0. Real reported Gini is never zeroed.
- Each inherited MOEA/D vector `(d,g,t)` becomes `(d,0,t)`. The all-zero vector
  originating from `(0,1,0)` is removed. The other nine channels retain their
  order and their original distance/MaxTime coefficients. They are **not
  renormalized**, merged, replaced or independently calibrated. Segment length
  is unchanged; cycling nine rather than ten active channels is part of ablation.
- AdaptiveInsertion masks Gini for default and explicitly supplied hints, so
  WORKLOAD_FOCUSED cannot be selected in NoEQ, including the mini-loop.
  The existing packing, distance and MaxTime rules/coefficients remain intact.
- Numeric operator reward values and weight adaptation formulas are unchanged.
  Reward classes are determined using the projected dominance and archive, so
  a Gini-only improvement cannot earn an improvement reward.
- Perturbation in the actual `first` source already chooses minimum vehicles /
  distance or a random archive member; it has **no direct Gini target strategy**.
  Its equity dependence is indirect, through the archive and stagnation signals.
  NoEQ uses its projected archive/signals with the same trigger, reheating,
  cooldown and jump rules. “Active” in the FP-PALNS table therefore refers to
  this indirect dependence, not to a direct best-Gini jump operator.
- The actual baseline convergence code uses **distance × Gini 2D HV**, despite
  its old startup message saying 3D. NoEQ uses **distance × MaxTime 2D HV** with
  inherited reference factor, distance/time reference bounds and convergence
  thresholds. Full mode preserves the legacy convergence calculation.
  `computeHypervolume()` still returns ex-post 3D HV; it is not a NoEQ control
  signal. Raw HV values of different dimensions/axes must not be compared.
- Optional Scatter Search crowding excludes Gini. UnifiedCostDestroy (currently
  not registered by default) excludes its WORKLOAD removal metric in NoEQ.
- No feasibility relaxation or acceptance of invalid routes is introduced.

## Build and reproduce

The branch defaults to NoEQ, including non-CMake builds. CMake propagates the
chosen policy to the library and its executables.

```sh
cmake -S . -B build-audit-noeq -DCMAKE_BUILD_TYPE=Release -DALNS_NO_EQUITY=ON
cmake --build build-audit-noeq -j4
ctest --test-dir build-audit-noeq --output-on-failure

# Reference mode for controlled comparisons, in a separate build directory:
cmake -S . -B build-audit-noeq-full -DCMAKE_BUILD_TYPE=Release -DALNS_NO_EQUITY=OFF
cmake --build build-audit-noeq-full -j4
ctest --test-dir build-audit-noeq-full --output-on-failure

# Fixed-iteration audit, new output directory each time:
build-audit-noeq/ALNSAuditRunner data/solomon/rc205_21.txt OUTPUT 3600 1 2000
python3 src/test/validate_audit_schedule.py data/solomon/rc205_21.txt OUTPUT OUTPUT/independent_validation.json
```

Use the same entry point, input, seed, budget and inherited numerical settings
for paired runs. The saved `TestALNS.cpp` has LS intensity 15 and non-dominated
reward 20; the pre-existing audit harness uses 25 and 25. Neither entry point's
settings have been retuned or changed for this branch. Audit results should not
be presented as a comparison using the `TestALNS` settings.

Logs identify the variant and search objectives. Final CSVs still report all
four true objectives **for the retained NoEQ search archive**; they do not
represent a separate four-objective archive of all visited candidates. In
particular, a Gini-only improvement at identical distance/MaxTime is not saved
as a new search point. No claim of improved runtime or solution quality follows
from this ablation; those require independent multi-seed experiments.

## Validation

Before modifications: existing WSL Release suite passed 9/9 CTest targets.
After modifications: both NoEQ and reference mode passed 10/10 CTest targets
(101 individual cases, including 11 search-policy cases). `TestALNS` Release
also built successfully. Compiler: WSL Ubuntu-22.04, GCC 11.4; identical Release
flags, with only `ALNS_NO_EQUITY=1` versus `0` differing in policy definitions.

Fixed 2,000-iteration audits, seed 1, existing audit parameters (no retuning):

| NoEQ instance | Exported solutions | Exported routes | Invalid (C++ / Python) |
|---|---:|---:|---:|
| r101_21 | 4 | 72 | 0 / 0 |
| r104_21 | 6 | 66 | 0 / 0 |
| c109_21 | 3 | 30 | 0 / 0 |
| rc106_21 | 7 | 91 | 0 / 0 |
| rc205_21 | 1 | 3 | 0 / 0 |
| Total | 21 | 262 | 0 / 0 |

Replay checks delivery coverage, battery/load conservation, partial charging,
time windows/depot return and all four reported objectives, including actual Gini.
These 2,000-iteration runs do not reach the perturbation cooldown; perturbation's
Gini independence is established by source review and the tested archive/reward
projection, not by claiming an observed perturbation event.

Reference restoration: on r101, `ALNS_NO_EQUITY=OFF` matched the frozen B4 binary
in all exported objective rows (excluding timing) and byte-for-byte schedules,
after 2,000 iterations spanning all ten original channels. Both returned 11
solutions / 187 routes and passed independent replay. Logged numerical settings
also matched NoEQ. The B4 binary represents the same solver core as `first`;
the later `first` commit only saved its `TestALNS` entry-point configuration.

The optional additional rc205 restoration comparison was stopped to avoid
extending an already completed correctness audit. Its frozen-baseline half
completed (18 solutions / 72 valid routes); the OFF-build half was cancelled.
**No rc205 restoration/equivalence result is claimed.** The rc205 **NoEQ** run
above completed fully and is unaffected. The reproduction script defaults to
r101 restoration; use `--reference-cases r101 rc205` for the optional extension.

Machine-readable evidence: [alns_noeq_validation.json](alns_noeq_validation.json).
Raw logs, CSVs and independent replay reports are kept locally in
`build-audit-noeq-validation-20261021/` (ignored build artifacts).
