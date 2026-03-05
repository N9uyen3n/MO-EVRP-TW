#include "../../../../include/alns/operators/repair/RegretKRepair.h"
#include <algorithm>
#include <cmath>
#include <vector>

RegretKRepair::RegretKRepair(std::shared_ptr<Instance> instance, int k,
                             double noiseParameter)
    : instance(instance), k_regret(k), noiseParam(noiseParameter) {}

std::string RegretKRepair::getName() const {
  return "Regret-" + std::to_string(k_regret) + " Repair";
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

void RegretKRepair::execute(Solution &solution,
                            const std::vector<int> &unservedCustomers,
                            std::mt19937 &rng) {
  std::vector<int> remainingCustomers = unservedCustomers;

  // numRoutes and numRemaining used inside the while loop now

  while (!remainingCustomers.empty()) {
    // ⭐ Recalculate adaptiveK every iteration since numRoutes can grow (new
    // routes created)
    int currentRoutes = solution.getNumRoutes();
    int curRemaining = static_cast<int>(remainingCustomers.size());
    int adaptiveK;
    if (curRemaining > 20) {
      adaptiveK = std::min(5, currentRoutes);
    } else if (curRemaining < 5) {
      // Increase K for tight insertions at the end
      adaptiveK = std::max(3, std::min(k_regret + 1, currentRoutes));
    } else {
      adaptiveK = std::min(k_regret, currentRoutes);
    }
    adaptiveK = std::max(1, adaptiveK);

    int bestCustId = -1;
    int bestRouteIdx = -1;
    int bestPos = -1;
    double maxRegret = -1.0;

    for (int custId : remainingCustomers) {
      // ⭐ Use adaptive K instead of fixed k_regret
      std::vector<InsertionCost> kBest =
          findKBestInsertions(custId, solution, rng, adaptiveK);

      if (kBest.empty())
        continue;

      double regretVal = 0.0;
      double bestCost = kBest[0].cost;

      if (kBest.size() == 1) {
        // Only one feasible insertion: highest urgency — must insert here
        regretVal = 100000.0; // Large constant to avoid overflow vs MAX/2
      } else {
        for (size_t i = 1; i < std::min((size_t)adaptiveK, kBest.size()); ++i) {
          regretVal += (kBest[i].cost - bestCost);
        }
        // If fewer insertions than K available, add a large bonus to indicate
        // urgency
        if ((int)kBest.size() < adaptiveK) {
          regretVal += 1e8; // Đủ lớn để ưu tiên, không overflow
        }
      }

      if (regretVal > maxRegret) {
        maxRegret = regretVal;
        bestCustId = custId;
        bestRouteIdx = kBest[0].routeIndex;
        bestPos = kBest[0].position;
      }
    }

    if (bestCustId != -1) {
      if (bestRouteIdx == -1) {
        createNewRouteForCustomer(solution, bestCustId, instance);
      } else {
        solution.getRoutes()[bestRouteIdx].addNode(bestCustId, bestPos);
        solution.getRoutes()[bestRouteIdx].evaluate();
      }
      remainingCustomers.erase(std::remove(remainingCustomers.begin(),
                                           remainingCustomers.end(),
                                           bestCustId),
                               remainingCustomers.end());
    } else {
      // Fallback: This shouldn't happen with the new route fallback option, but
      // keep just in case
      if (!remainingCustomers.empty()) {
        createNewRouteForCustomer(solution, remainingCustomers[0], instance);
        remainingCustomers.erase(remainingCustomers.begin());
      }
    }
  }
}

std::vector<RegretKRepair::InsertionCost>
RegretKRepair::findKBestInsertions(int customerId, Solution &solution,
                                   std::mt19937 &rng, int adaptiveK) {

  struct Candidate {
    int routeIdx;
    size_t position;
    double estimatedCost;
    bool operator<(const Candidate &other) const {
      return estimatedCost < other.estimatedCost;
    }
  };

  std::vector<Candidate> candidates;
  auto &routes = solution.getRoutes();

  // Tier 1 & 2: Generate candidates with fast, approximate checks
  for (int r = 0; r < routes.size(); ++r) {
    for (size_t pos = 1; pos < routes[r].getNodes().size(); ++pos) {
      if (!routes[r].canPossiblyInsert(customerId, pos)) {
        continue;
      }
      auto fastResult = routes[r].fastForwardCheck(customerId, pos);
      if (fastResult.isFeasible) {
        candidates.push_back({r, pos, fastResult.deltaDistance});
      }
    }
  }

  if (candidates.empty()) {
    return {};
  }

  std::sort(candidates.begin(), candidates.end());

  // Tier 3: Verify top candidates with exact cost
  std::vector<InsertionCost> exactInsertions;
  // Increase VERIFY_COUNT for better exploration, min 5 for random instances
  const int VERIFY_COUNT = std::max(5, adaptiveK * 2);

  for (int i = 0; i < std::min((int)candidates.size(), VERIFY_COUNT); ++i) {
    auto &candidate = candidates[i];
    auto exactResult = routes[candidate.routeIdx].checkInsertionCost(
        customerId, candidate.position);

    if (exactResult.isFeasible) {
      // TW-aware cost: prioritize customers whose insertion causes less
      // waiting and doesn't disrupt tight time windows.
      // - deltaWaitTime < 0 means we are "absorbing" existing slack → good.
      // - deltaWaitTime > 0 means we push other customers to wait more → bad.
      // - deltaChargeAmount > 0 means energy strain (light penalty only for
      // Regret).
      double twUrgencyCost = std::max(0.0, exactResult.deltaWaitTime) * 2.0;

      double baseCost =
          1.0 * exactResult.deltaDistance +
          2.0 * twUrgencyCost // TW sensitivity (Regret's specialty)
          + 0.1 * exactResult.deltaChargeAmount; // Very light energy pressure

      if (noiseParam > 0) {
        std::uniform_real_distribution<double> dist(-noiseParam, noiseParam);
        double d_noise = std::max(0.0, 1.0 + dist(rng));
        double t_noise = std::max(0.0, 1.0 + dist(rng));
        baseCost = (1.0 * exactResult.deltaDistance * d_noise) +
                   (2.0 * twUrgencyCost * t_noise) +
                   (0.1 * exactResult.deltaChargeAmount);
      }
      baseCost = std::max(0.0, baseCost); // Đảm bảo baseCost không âm
      exactInsertions.push_back(
          {candidate.routeIdx, candidate.position, baseCost, true});
    }
  }

  // Tính cost của new route dựa trên khoảng cách thực tế depot→cust→depot
  double dist_to = instance->getDistance(0, customerId);
  double dist_back = instance->getDistance(customerId, 0);
  const double BIG_M = 1e6; // Không thể bị vượt bởi bất kỳ baseCost nào
  double newRouteCost = dist_to + dist_back + BIG_M;
  exactInsertions.push_back({-1, 1, newRouteCost, true});

  if (exactInsertions.empty()) {
    return {};
  }

  // Sort again based on exact costs
  std::sort(exactInsertions.begin(), exactInsertions.end(),
            [](const InsertionCost &a, const InsertionCost &b) {
              return a.cost < b.cost;
            });

  // Return only the top k results (using adaptive K)
  if (exactInsertions.size() > adaptiveK) {
    exactInsertions.resize(adaptiveK);
  }

  return exactInsertions;
}
