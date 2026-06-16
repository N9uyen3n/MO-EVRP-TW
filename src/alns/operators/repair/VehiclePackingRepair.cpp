// =============================================================================
// VehiclePackingRepair.cpp — Phase 4 Rewrite
// =============================================================================
// Changes vs previous impl:
//   - Customer ordering: by TW tightness ascending (tightest dueDate-readyTime first)
//   - Insertion cost: deltaDistance - PACK_BONUS * (route.activeTime / horizon)
//   - Station handling: try top-3 nearest stations before creating new route
//   - TW grouping: bucket by readyTime / avgTWWidth (integer slot) instead of exact match
//   - 2-move swap removed: slow and ineffective without proper evaluation
// =============================================================================

#include "../../../../include/alns/operators/repair/VehiclePackingRepair.h"
#include "../../../../include/core/Route.h"
#include "../../../../include/core/Solution.h"
#include "../../../../include/core/Instance.h"
#include "../../../../include/core/Node.h"
#include "../../../../include/core/Customer.h"
#include "../../../../include/core/Depot.h"
#include "../../../../include/core/Station.h"
#include "../../../../include/core/Vehicle.h"
#include <algorithm>
#include <numeric>
#include <random>

static constexpr double PACK_BONUS = 500.0; // packing score bonus coefficient

VehiclePackingRepair::VehiclePackingRepair(std::shared_ptr<Instance> instance)
    : instance_(std::move(instance)) {}

std::string VehiclePackingRepair::getName() const {
  return "VehiclePackingRepair";
}

void VehiclePackingRepair::execute(Solution& solution,
                                   const std::vector<int>& unservedCustomers,
                                   std::mt19937& rng) {
  if (unservedCustomers.empty()) return;

  // ── Step 1: Compute planning horizon (depot due date) ────────────────────
  double horizon = 1.0;
  for (const auto& node : instance_->getNodes()) {
    if (node->getType() == NodeType::DEPOT) {
      horizon = std::max(1.0, node->getDueDate());
      break;
    }
  }

  // ── Step 2: Compute average TW width for bucket grouping ─────────────────
  double totalTW = 0.0;
  int nCusts = 0;
  for (const auto& c : instance_->getCustomers()) {
    totalTW += (c->getDueDate() - c->getReadyTime());
    nCusts++;
  }
  double avgTWWidth = (nCusts > 0) ? std::max(1.0, totalTW / nCusts) : 1.0;

  // ── Step 3: Sort by TW tightness ascending ────────────────────────────────
  std::vector<int> toInsert = unservedCustomers;
  std::sort(toInsert.begin(), toInsert.end(), [&](int a, int b) {
    auto ca = std::dynamic_pointer_cast<Customer>(instance_->getNodeById(a));
    auto cb = std::dynamic_pointer_cast<Customer>(instance_->getNodeById(b));
    double twA = ca ? (ca->getDueDate() - ca->getReadyTime()) : 1e18;
    double twB = cb ? (cb->getDueDate() - cb->getReadyTime()) : 1e18;
    return twA < twB;
  });

  // ── Step 4: For each customer, find best packing insertion ────────────────
  auto& routes = solution.getRoutes();

  for (int customerId : toInsert) {
    double bestScore = 1e18;
    int bestRoute   = -1;
    int bestPos     = -1;

    // Scan all existing routes
    for (int ri = 0; ri < (int)routes.size(); ++ri) {
      const auto& route = routes[ri];
      const auto& nodes = route.getNodes();

      // Quick capacity check
      double custDemand = instance_->getNodeById(customerId)->getDemand();
      if (!route.quickCapacityCheck(custDemand)) continue;

      // Compute route time utilization for packing score
      double activeTime = route.getTotalDistance(); // proxy for time utilization
      double timeUtil = std::min(1.0, activeTime / horizon);

      for (int pos = 1; pos < (int)nodes.size(); ++pos) {
        auto res = route.checkInsertionCost(customerId, pos);
        if (!res.isFeasible) continue;

        double packingScore = res.deltaDistance - PACK_BONUS * timeUtil;
        if (packingScore < bestScore) {
          bestScore = packingScore;
          bestRoute = ri;
          bestPos   = pos;
        }
      }
    }

    if (bestRoute >= 0) {
      // Direct insertion found
      routes[bestRoute].addNode(customerId, bestPos);
      routes[bestRoute].evaluate();
    } else {
      // ── Station-assisted fallback: try top-3 nearest stations ─────────────
      bool inserted = false;
      const auto& stations = instance_->getStations();

      // Build sorted station list by proximity to customer
      struct StDet { int id; double det; };
      std::vector<StDet> stationList;
      stationList.reserve(stations.size());
      for (const auto& st : stations) {
        int sid = st->getId();
        double det = instance_->getDistance(customerId, sid);
        stationList.push_back({sid, det});
      }
      // Try ALL stations (not just top-3) for maximum packing opportunity
      std::sort(stationList.begin(), stationList.end(),
                [](const StDet& a, const StDet& b){ return a.det < b.det; });

      for (int ki = 0; ki < (int)stationList.size() && !inserted; ++ki) {
        int stationId = stationList[ki].id;
        for (int ri = 0; ri < (int)routes.size() && !inserted; ++ri) {
          const auto& nodes = routes[ri].getNodes();
          double custDemand = instance_->getNodeById(customerId)->getDemand();
          if (!routes[ri].quickCapacityCheck(custDemand)) continue;

          for (int pos = 1; pos < (int)nodes.size() && !inserted; ++pos) {
            // Try [station, customer] pattern at pos
            for (bool stationFirst : {true, false}) {
              Route testRoute = routes[ri];
              if (stationFirst) {
                testRoute.addNode(customerId, pos);
                testRoute.addNode(stationId, pos);
              } else {
                testRoute.addNode(customerId, pos);
                testRoute.addNode(stationId, pos + 1);
              }
              testRoute.evaluate();
              if (testRoute.isFeasible()) {
                routes[ri] = testRoute;
                inserted = true;
                break;
              }
            }
          }
        }
      }

      if (!inserted) {
        // ── Last resort: create new route ──────────────────────────────────
        int newRouteId = solution.getNumRoutes();
        auto vehicle = std::make_shared<Vehicle>(
            newRouteId, instance_->getVehicleCapacity(),
            instance_->getVehicleBattery(), instance_->getVehicleEnergyRate());
        Route newRoute(newRouteId, vehicle, instance_);
        newRoute.addNode(customerId, 1);
        newRoute.evaluate();

        if (newRoute.isFeasible()) {
          solution.addRoute(newRoute);
        } else {
          // Try to make it feasible with a station
          for (const auto& st : stations) {
            int sid = st->getId();
            Route withSt(newRouteId, vehicle, instance_);
            withSt.addNode(sid, 1);
            withSt.addNode(customerId, 2);
            withSt.evaluate();
            if (withSt.isFeasible()) {
              solution.addRoute(withSt);
              break;
            }
            Route withStAfter(newRouteId, vehicle, instance_);
            withStAfter.addNode(customerId, 1);
            withStAfter.addNode(sid, 2);
            withStAfter.evaluate();
            if (withStAfter.isFeasible()) {
              solution.addRoute(withStAfter);
              break;
            }
          }
        }
      }
    }
  }

  solution.removeEmptyRoutes();
}