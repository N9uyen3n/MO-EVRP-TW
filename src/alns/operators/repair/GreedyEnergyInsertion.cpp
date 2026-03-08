#include "../../../../include/alns/operators/repair/GreedyEnergyInsertion.h"
#include "../../../../include/core/Instance.h"
#include "../../../../include/core/Solution.h"
#include "../../../../include/core/Vehicle.h" // [QUAN TRỌNG] Cần để tạo xe mới
#include <algorithm>
#include <limits>
#include <vector>

GreedyEnergyInsertion::GreedyEnergyInsertion(std::shared_ptr<Instance> inst)
    : instance(inst) {}

// getName
std::string GreedyEnergyInsertion::getName() const {
  return "Greedy Energy Insertion (Enhanced)";
}

// Helper function from GreedyDistanceInsertion, assuming it's moved to a common
// place or duplicated.
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

void GreedyEnergyInsertion::execute(Solution &solution,
                                    const std::vector<int> &unservedCustomers,
                                    std::mt19937 &rng) {
  std::vector<int> customers = unservedCustomers;

  // Sort by energy difficulty: customers far from depot with high demand are
  // hardest to serve (require most energy) → insert them first to give them
  // priority. This is more effective than random shuffle for an energy-focused
  // operator.
  // ⭐ Fix: dùng raw pointer thay static_pointer_cast trong sort comparator
  // static_pointer_cast tạo shared_ptr mới (atomic refcount) mỗi comparison → chậm
  std::sort(customers.begin(), customers.end(), [&](int a, int b) {
    const Node* nodeA = instance->getNodeById(a).get();
    const Node* nodeB = instance->getNodeById(b).get();
    double difficultyA = instance->getDistance(0, a) * nodeA->getDemand();
    double difficultyB = instance->getDistance(0, b) * nodeB->getDemand();
    return difficultyA > difficultyB; // Hardest first
  });

  auto &routes = solution.getRoutes();

  for (int customerId : customers) {

    auto customerNode = instance->getNodeById(customerId);
    double customerDemand = customerNode->getDemand(); // raw pointer (Node::getDemand virtual)

    std::vector<int> feasibleRoutesIndices;
    for (int r = 0; r < routes.size(); ++r) {
      if (routes[r].quickCapacityCheck(customerDemand)) {
        feasibleRoutesIndices.push_back(r);
      }
    }

    if (feasibleRoutesIndices.empty()) {
      createNewRouteForCustomer(solution, customerId, instance);
      continue;
    }

    auto tryStationAssisted = [&](int rIdx) -> bool {
      int nearStation = instance->getNearestStationId(customerId);
      if (nearStation == -1)
        return false;
      const auto &nodes = routes[rIdx].getNodes();
      const double battCap    = routes[rIdx].getVehicle()->getBatteryCapacity();
      const double energyRate = routes[rIdx].getVehicle()->getEnergyConsumptionRate();
      const auto  &states     = routes[rIdx].getStates();
      if (states.empty()) routes[rIdx].evaluate();

      for (size_t pos = 1; pos < nodes.size(); ++pos) {
        int prevId = nodes[pos - 1];
        int nextId = nodes[pos];

        // ⭐ Fix: điều kiện pre-filter chỉ check battery đến station (O(1)).
        // Không estimate battery sau charge → tránh false reject.
        // Deep-copy verify sẽ check đầy đủ feasibility với partial charging.
        {
          double battAtPrev = states[pos - 1].remainingBattery;
          double distToSt   = instance->getDistance(prevId, nearStation) * energyRate;
          if (battAtPrev >= distToSt) { // có thể đến station
            Route copy1 = routes[rIdx];
            copy1.addNode(nearStation, pos);
            copy1.addNode(customerId, pos + 1);
            copy1.evaluate();
            if (copy1.isFeasible()) {
              routes[rIdx] = copy1;
              return true;
            }
          }
        }
        // [customer, station]
        {
          double battAtPrev = states[pos - 1].remainingBattery;
          double distToCust = instance->getDistance(prevId, customerId) * energyRate;
          if (battAtPrev >= distToCust) { // có thể đến customer
            Route copy2 = routes[rIdx];
            copy2.addNode(customerId, pos);
            copy2.addNode(nearStation, pos + 1);
            copy2.evaluate();
            if (copy2.isFeasible()) {
              routes[rIdx] = copy2;
              return true;
            }
          }
        }
      }
      return false;
    };

    struct Candidate {
      int routeIdx;
      size_t position;
      double estimatedCost; // For Energy, we use deltaDistance as a proxy
      bool operator<(const Candidate &other) const {
        return estimatedCost < other.estimatedCost;
      }
    };

    std::vector<Candidate> candidates;

    for (int r : feasibleRoutesIndices) {
      const auto &nodes = routes[r].getNodes();
      for (size_t pos = 1; pos < nodes.size(); ++pos) {

        if (!routes[r].canPossiblyInsert(customerId, pos)) {
          continue;
        }

        auto fastResult = routes[r].fastForwardCheck(customerId, pos);

        if (fastResult.isFeasible) {
          // NOTE: The true cost is deltaChargeAmount, but fastForwardCheck
          // doesn't calculate it. We use deltaDistance as a proxy to find
          // promising candidates. The final verification will use the true
          // cost.
          candidates.push_back({r, pos, fastResult.deltaDistance});
        }
      }
    }

    if (candidates.empty()) {
      bool insertedWithStation = false;
      for (int r : feasibleRoutesIndices) {
        if (tryStationAssisted(r)) {
          insertedWithStation = true;
          break;
        }
      }
      if (!insertedWithStation) {
        createNewRouteForCustomer(solution, customerId, instance);
      }
      continue;
    }

    std::sort(candidates.begin(), candidates.end());

    // Dynamic MAX_VERIFY: check at least 3, at most 10, but scale with
    // candidate pool size
    int MAX_VERIFY = std::max(3, std::min(10, (int)(candidates.size() * 0.3)));
    bool inserted = false;

    // We need to find the best among the verified candidates, not just the
    // first one.
    double minExactCost = std::numeric_limits<double>::max();
    int bestRouteIdx = -1;
    size_t bestPos = -1;

    for (int i = 0; i < std::min(MAX_VERIFY, (int)candidates.size()); ++i) {
      auto &candidate = candidates[i];

      auto exactResult = routes[candidate.routeIdx].checkInsertionCost(
          customerId, candidate.position);

      if (exactResult.isFeasible) {
        auto slack = routes[candidate.routeIdx].getEnergySlack();
        double energySlackAtPos = 0.0;
        if (candidate.position > 0 && candidate.position - 1 < slack.size()) {
          energySlackAtPos = slack[candidate.position - 1];
        }

        double battCap = routes[candidate.routeIdx].getVehicle()->getBatteryCapacity();

        // 1. Primary: energy consumed (always present)
        double energyCost = exactResult.deltaEnergyConsumption;

        // 2. Charging cost: penalty proportional to charge amount needed
        // ⭐ Fix: scale chargingTimeCost by battCap để calibrate với energyCost
        if (exactResult.deltaChargeAmount > 1e-6) {
          // deltaChargeAmount in same units as energy → directly addable
          energyCost += exactResult.deltaChargeAmount * 0.3;
        }

        // 3. Station penalty: only when energy consumption exceeds available slack
        // ⭐ Fix: stationPenalty scaled by battCap (không hardcode 3.0)
        bool likelyNeedsStation = (exactResult.deltaEnergyConsumption > energySlackAtPos * 0.8);
        if (likelyNeedsStation) {
          energyCost += battCap * 0.05; // ~5% of battery capacity as penalty — scale-aware
        }

        // 4. Bottleneck penalty: scaled by battCap for consistency
        if (candidate.position > 0 && candidate.position - 1 < slack.size()) {
          double threshold   = 0.15 * battCap;
          double localSlack  = slack[candidate.position - 1];
          if (localSlack < threshold) {
            // ⭐ Fix: normalize bottleneckPenalty → ∈ [0, 1], weight calibrated
            double bottleneckPenalty = (threshold - localSlack) / std::max(threshold, 1e-9);
            energyCost += battCap * 0.03 * bottleneckPenalty; // ~3% battCap max
          }
        }

        if (energyCost < minExactCost) {
          minExactCost = energyCost;
          bestRouteIdx = candidate.routeIdx;
          bestPos = candidate.position;
          inserted = true;
        }
      }
    }

    if (inserted) {
      routes[bestRouteIdx].addNode(customerId, bestPos);
      routes[bestRouteIdx].evaluate();
    } else {
      bool insertedWithStation = false;
      for (int r : feasibleRoutesIndices) {
        if (tryStationAssisted(r)) {
          insertedWithStation = true;
          break;
        }
      }
      if (!insertedWithStation) {
        createNewRouteForCustomer(solution, customerId, instance);
      }
    }
  }
}