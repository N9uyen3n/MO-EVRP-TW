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
  std::sort(customers.begin(), customers.end(), [&](int a, int b) {
    auto custA = std::static_pointer_cast<Customer>(instance->getNodeById(a));
    auto custB = std::static_pointer_cast<Customer>(instance->getNodeById(b));
    double difficultyA = instance->getDistance(0, a) * custA->getDemand();
    double difficultyB = instance->getDistance(0, b) * custB->getDemand();
    return difficultyA > difficultyB; // Hardest first
  });

  auto &routes = solution.getRoutes();

  for (int customerId : customers) {

    auto customerNode = instance->getNodeById(customerId);
    double customerDemand =
        std::static_pointer_cast<Customer>(customerNode)->getDemand();

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
      for (size_t pos = 1; pos < nodes.size(); ++pos) {
        // [station, customer]
        Route copy1 = routes[rIdx];
        copy1.addNode(nearStation, pos);
        copy1.addNode(customerId, pos + 1);
        copy1.evaluate();
        if (copy1.isFeasible()) {
          routes[rIdx] = copy1;
          return true;
        }
        // [customer, station]
        Route copy2 = routes[rIdx];
        copy2.addNode(customerId, pos);
        copy2.addNode(nearStation, pos + 1);
        copy2.evaluate();
        if (copy2.isFeasible()) {
          routes[rIdx] = copy2;
          return true;
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
        // ⭐ CHỈNH SỬA LẠI: Penalty chính xác và Slack-preshing
        // Lấy slack energy tại điểm dự kiến chèn (dùng position - 1 vì mảng
        // slack nhỏ hơn mảng nodesRoute 1 đơn vị)
        auto slack = routes[candidate.routeIdx].getEnergySlack();
        double energySlackAtPos = 0.0;
        if (candidate.position > 0 && candidate.position - 1 < slack.size()) {
          energySlackAtPos = slack[candidate.position - 1];
        }

        // 1. Energy consumption (always present when traveling)
        double energyConsumption = exactResult.deltaEnergyConsumption;

        // 2. Charging time cost (if charging is needed)
        // Chỉ penalty time charging nếu thực tế sạc lượng đáng kể
        double chargingTimeCost =
            exactResult.deltaChargeAmount > 1e-6
                ? exactResult.deltaChargeAmount * 0.5 // Charging time penalty
                : 0.0;

        // 3. Station penalty (discourage adding new stations)
        // Chỉ penalty khi Mức tiêu thụ thực sự vượt ngưỡng Slack của route
        // (buộc phải lắp thêm trạm)
        bool likelyNeedsStation = (energyConsumption > energySlackAtPos * 0.8);
        double stationPenalty = likelyNeedsStation ? 3.0 : 0.0;

        // Total energy cost base
        double energyCost = 1.0 * energyConsumption  // Primary: energy consumed
                            + 0.5 * chargingTimeCost // Secondary: time cost
                            + stationPenalty; // Tertiary: station penalty

        // 4. Bottleneck penalty (proportional to energy tightness at insertion
        // point)
        if (candidate.position > 0 && candidate.position - 1 < slack.size()) {
          double batteryCapacity =
              routes[candidate.routeIdx].getVehicle()->getBatteryCapacity();
          double threshold = 0.15 * batteryCapacity;
          double localSlack = slack[candidate.position - 1];
          double bottleneckPenalty =
              std::max(0.0, threshold - localSlack) / std::max(threshold, 1e-9);
          // Giảm weight từ 5.0 xuống 2.0 để tránh over-penalization
          energyCost += 2.0 * bottleneckPenalty;
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