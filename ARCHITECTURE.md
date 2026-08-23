# ARCHITECTURE.md
Verbose reference — only load when working on core domain APIs, operators, or ALNS config.

---

## Directory Layout

```
include/
  core/       Node.h, Customer.h, Depot.h, Station.h, Vehicle.h, Route.h, Instance.h, Solution.h
  alns/       ALNSSolver.h, LocalSearch.h, ParetoArchive.h, ScatterSearch.h, IOperator.h, SolutionPool.h
    operators/
      destroy/  ShawDestroy.h, RouteMergingDestroy.h, TimeSlackDestroy.h, UnifiedCostDestroy.h,
                InefficientRouteRemoval.h, RandomRemoval.h, RandomRouteRemoval.h, TargetedStationRemoval.h
      repair/   AdaptiveInsertion.h, RegretKRepair.h, SmartStationRepair.h, SmartTimeAwareStationRepair.h,
                ChargingAwareRouteBuilder.h, GreedyEnergyInsertion.h, ParetoFocusRepair.h
  io/         Parser.h
  app/        AppController.h
  logger/     ComprehensiveLogger.h, NullLogger.h
src/          (mirrors include/ structure, plus src/test/, src/tuning/)
data/solomon/ Schneider 2014 benchmark instances (c/r/rc series, 5/10/15/21 customers)
```

---

## Build Targets

| Target | Entry point | Purpose |
|---|---|---|
| `TestALNS` | `src/test/TestALNS.cpp` | Primary test runner |
| `test19` | `src/main.cpp` | Main executable |
| `TuningApp` | `src/tuning/TuningApp.cpp` | Parameter tuning |
| `BenchmarkTest2` | `src/test/BenchmarkTest2.cpp` | Batch benchmarks |

All targets link against `alns_lib` (static: `src/core/`, `src/alns/`, `src/io/`, `src/utils/`, `src/logger/`, `src/app/`).

---

## Core Domain APIs

### `Instance.h`
- **Data**: `getCustomers()`, `getStations()`, `getNodes()`, `getStationIds()`
- **Vehicle**: `getVehicleCapacity()`, `getVehicleBattery()`, `getVehicleEnergyRate()`, `getVehicleVelocity()`
- **Lookups**: `getNodeById(int)`, `getNodeRaw(int)` *(fast raw ptr)*, `getNodeType(int)` *(O(1) cache)*, `getNearestStationId(int)`
- **Matrix**: `getDistance(i,j)`, `getTime(i,j)` — O(1)
- **Globals**: `getMaxDistance()`, `getMaxTimeWindow()`, `getMaxDemand()`

### `Route.h`
- **State**: `getNodes()`, `getCustomers()`, `getStates()`, `getNodeAt(pos)`, `getLastNodeId()`, `size()`
- **Modifiers**: `addNode(id, pos)`, `addNode(id)`, `removeNode(pos)`, `clear()`, `reverseNodes(i,j)`, `pruneRedundantStations()`
- **Metrics**: `getTotalDistance()`, `getTotalTime()`, `getTotalWaitTime()`, `getTotalChargeTime()`, `getActiveTime()`, `getTotalDemand()`, `getCentroidX()`, `getCentroidY()`
- **Validation**: `evaluate()`, `isFeasible()`
- **Insertion**: `checkInsertionCost(id, pos)` → `InsertionResult`, `canPossiblyInsert(id, pos)`, `fastForwardCheck(id, pos)`, `quickCapacityCheck(demand)`
- **Advanced**: `getTimeSlack()`, `getEnergySlack()`, `getBottleneckNodes()`, `getRedundantStations()`, `getMinBatteryReq()`

### `Solution.h`
- **Routes**: `getRoutes()`, `getNumRoutes()`, `addRoute(route)`, `removeRoute(idx)`, `removeEmptyRoutes()`
- **Customers**: `removeCustomer(id)` *(O(R) search & remove)*
- **Metrics**: `getTotalVehicles()`, `getTotalDistance()`, `getWorkloadGini()`, `getMaxTime()`, `getTotalTime()`, `getAverageRouteTime()`
- **Validation**: `evaluateRoutes()`, `isFeasible()`, `dominates(other)`
- **VR Hints**: `getHint()`, `setHint(SolutionHint)`, `hasValidHint()`

---

## Local Search Phases (`LocalSearch.h`)

| Phase | Operators | Goal |
|---|---|---|
| **1** — Distance VND | Relocate, Swap, Intra-2-Opt, Inter-2-Opt, Or-Opt, CrossExchange | Minimize distance. First-improvement, tiered filtering. |
| **2** — Charging opt | `removeRedundantStations`, `repositionStations`, `searchStationSwap` | Clean up charging stops |
| **3** — Vehicle Reduction | `ejectionChain`, `tryEliminateSmallestRoute`, `runSmartMultiRouteMerge` | **PRIMARY GOAL** — must run more often |

---

## Active Operators

**Destroy (5 active):**

| Operator | Weight | Role |
|---|---|---|
| `VehicleReductionAwareDestroy` | 3.5 | Targets weak routes, mergable route pairs, energy bottlenecks |
| `RouteMergingDestroy` | 3.0 | Remove pairs that could merge (Z₁ focus) |
| `EnergyCriticalRemoval` | 2.5 | Target customers with severe battery shortfall |
| `TimeSlackDestroy` | 2.5 | Remove scheduling bottlenecks |
| `RandomRemoval` | 2.0 | Diversification |

**Repair (4 active):**

| Operator | Weight | Role |
|---|---|---|
| `RegretKRepair` | 2.5 | Regret-K insertion with lookahead |
| `ChargingAwareRouteBuilder` | 2.0 | Beam-search constructive |
| `AdaptiveInsertion` | 2.0 | Mode-based: DISTANCE / VEHICLE_PACKING / WORKLOAD / MAXTIME |
| `GreedyEnergyInsertion` | 1.5 | Energy-focused insertion |

**Inactive (commented out):**
- `TargetedStationRemoval`, `UnifiedCostDestroy`, `ShawDestroy`, `InefficientRouteRemoval`, `RandomRouteRemoval`
- `SmartStationRepair`, `VehiclePackingRepair`, `ParetoFocusRepair`, `SmartTimeAwareStationRepair`

---

## Key Config Parameters (`TestALNS.cpp`)

```
maxIterations        = 25000
segmentIterations    = 200
startTemperature     = 200.0
coolingRate          = 0.9993
minTemperature       = 0.05
minRemoval           = 0.15
maxRemoval           = 0.55
localSearchIntensity = 20  (%)
hvImprovementThreshold = 0.001
hvStagnationLimit    = 20
maxTime              = 521000 ms (~8.68 min)
```
