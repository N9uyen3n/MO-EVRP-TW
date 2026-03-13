#include "../../../../include/alns/operators/repair/ParetoFocusRepair.h"
#include "../../../../include/core/Instance.h"
#include "../../../../include/core/Solution.h"
#include "../../../../include/core/Vehicle.h"
#include <algorithm>
#include <iostream>
#include <limits>
#include <numeric>


ParetoFocusRepair::ParetoFocusRepair(std::shared_ptr<Instance> inst)
    : instance(inst) {}

std::string ParetoFocusRepair::getName() const {
  return "Pareto Focus Repair (Vehicle Minimizing)";
}

// Helper function
namespace {
void createNewRouteForCustomer(Solution &solution, int customerId,
                               std::shared_ptr<Instance> instance) {
  int newRouteId = solution.getNumRoutes();
  auto vehicle = std::make_shared<Vehicle>(
      newRouteId, instance->getVehicleCapacity(), instance->getVehicleBattery(),
      instance->getVehicleEnergyRate());
  Route newRoute(newRouteId, vehicle, instance);
  newRoute.addNode(customerId, 1);
  newRoute.evaluate();
  solution.addRoute(newRoute);
}
} // namespace

void ParetoFocusRepair::execute(Solution &solution,
                                const std::vector<int> &unservedCustomers,
                                std::mt19937 &rng) {
  auto &routes = solution.getRoutes();

  // =====================================================================
  // STEP 1: Sort customers theo TW tightest first
  // Customers khó (TW tight) insert trước → tránh bị blocked sau
  // =====================================================================
  std::vector<int> customers = unservedCustomers;
  std::sort(customers.begin(), customers.end(), [&](int a, int b) {
    auto na = instance->getNodeById(a);
    auto nb = instance->getNodeById(b);
    double twA = na->getDueDate() - na->getReadyTime();
    double twB = nb->getDueDate() - nb->getReadyTime();
    if (std::abs(twA - twB) < 1e-6) {
      return na->getDueDate() < nb->getDueDate();
    }
    return twA < twB; // Tightest first
  });

  // =====================================================================
  // Helper: Calculate route time utilization (for Packing Strategy)
  // =====================================================================
  double timeHorizon = instance->getNodeById(0)->getDueDate();
  if (timeHorizon < 1e-6)
    timeHorizon = 230.0;
  auto getRouteUtilization = [&](const Route &r) -> double {
    if (r.getCustomers().empty())
      return 0.0;
    return r.getTotalTime() / timeHorizon;
  };

  for (int customerId : customers) {
    auto customerNode = instance->getNodeById(customerId);
    double demand =
        std::static_pointer_cast<Customer>(customerNode)->getDemand();

    // =================================================================
    // STEP 3: Build candidates từ TẤT CẢ routes, sort theo packing score
    // Packing score = ưu tiên route gần đầy + insertion cost thấp
    // =================================================================
    struct Candidate {
      int routeIdx;
      size_t position;
      double packingScore; // Lower = better
    };
    std::vector<Candidate> candidates;

    for (int r = 0; r < (int)routes.size(); ++r) {
      if (!routes[r].quickCapacityCheck(demand))
        continue;

      const auto &nodes = routes[r].getNodes();
      for (size_t pos = 1; pos < nodes.size(); ++pos) {
        // Pre-check feasibility
        if (!routes[r].canPossiblyInsert(customerId, pos))
          continue;

        auto res = routes[r].checkInsertionCost(customerId, pos);
        if (!res.isFeasible)
          continue;

        // Packing score: penalize creating new time gaps,
        // reward inserting into fuller routes
        // utilBonus: càng đầy càng âm (tốt)
        double utilBonus = -20.0 * getRouteUtilization(routes[r]);
        double costPenalty = res.deltaDistance;
        double packingScore = costPenalty + utilBonus;

        candidates.push_back({r, pos, packingScore});
      }
    }

    // =================================================================
    // STEP 4: Insert vào best candidate
    // =================================================================
    if (!candidates.empty()) {
      auto best = std::min_element(candidates.begin(), candidates.end(),
                                   [](const Candidate &a, const Candidate &b) {
                                     return a.packingScore < b.packingScore;
                                   });
      routes[best->routeIdx].addNode(customerId, best->position);
      routes[best->routeIdx].evaluate();
    } else {
      // =============================================================
      // STEP 5: Fallback — thử thêm station vào route hiện tại
      // trước khi tạo route mới
      // =============================================================
      bool insertedWithStation = false;
      for (int r = 0; r < (int)routes.size(); ++r) {
        if (!routes[r].quickCapacityCheck(demand))
          continue;

        // Tìm nearest station từ customer để hỗ trợ
        int nearStation = instance->getNearestStationId(customerId);
        if (nearStation == -1)
          continue;

        // Thử station-assisted chèn trực tiếp [station, customer] hoặc
        // [customer, station]
        for (size_t pos = 1; pos < routes[r].getNodes().size(); ++pos) {
          // Try [station, customer]
          Route copy1 = routes[r];
          copy1.addNode(nearStation, pos);
          copy1.addNode(customerId, pos + 1);
          copy1.evaluate();
          if (copy1.isFeasible()) {
            routes[r] = copy1;
            insertedWithStation = true;
            break;
          }

          // Try [customer, station]
          Route copy2 = routes[r];
          copy2.addNode(customerId, pos);
          copy2.addNode(nearStation, pos + 1);
          copy2.evaluate();
          if (copy2.isFeasible()) {
            routes[r] = copy2;
            insertedWithStation = true;
            break;
          }
        }
        if (insertedWithStation)
          break;
      }

      if (!insertedWithStation) {
        // Last resort: tạo route mới
        createNewRouteForCustomer(solution, customerId, instance);
      }
    }
  }
}
