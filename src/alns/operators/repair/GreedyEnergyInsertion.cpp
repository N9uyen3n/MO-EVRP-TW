#include "../../../../include/alns/operators/repair/GreedyEnergyInsertion.h"
#include "../../../../include/core/Instance.h"
#include "../../../../include/core/Solution.h"
#include "../../../../include/core/Vehicle.h" // [QUAN TRONG] Can de tao xe moi
#include <algorithm>
#include <optional>
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

  // FIX #2: Kiểm tra feasibility trước khi add route.
  // Khách hàng đơn lẻ vẫn có thể vi phạm TW hoặc energy.
  if (newRoute.isFeasible()) {
    solution.addRoute(newRoute);
    return;
  }

  // FIX #5: Dung all-stations sorted theo detour thay vi getNearestStationId (Euclidean)
  // Station Euclidean gần nhất không chắc có min-detour trong context cua route nay
  struct StDet { int id; double det; };
  std::vector<StDet> sortedStats;
  const auto& allStations = instance->getStations();
  for (const auto& st : allStations) {
    int sid = st->getId();
    // Voi solo route: metric la min(dist(depot,st)+dist(st,cust), dist(cust,st)+dist(st,depot))
    double detBefore = instance->getDistance(0, sid) + instance->getDistance(sid, customerId);
    double detAfter  = instance->getDistance(customerId, sid) + instance->getDistance(sid, 0);
    sortedStats.push_back({sid, std::min(detBefore, detAfter)});
  }
  std::sort(sortedStats.begin(), sortedStats.end(),
            [](const StDet& a, const StDet& b){ return a.det < b.det; });

  for (const auto& sd : sortedStats) {
    Route r1(newRouteId, vehicle, instance);
    r1.addNode(sd.id, 1);
    r1.addNode(customerId, 2);
    r1.evaluate();
    if (r1.isFeasible()) {
      solution.addRoute(r1);
      return;
    }
    Route r2(newRouteId, vehicle, instance);
    r2.addNode(customerId, 1);
    r2.addNode(sd.id, 2);
    r2.evaluate();
    if (r2.isFeasible()) {
      solution.addRoute(r2);
      return;
    }
  }
  // Neu van fail: bo qua (khong add infeasible route)

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
  // FIX #12: Tieu chi sap xep chi dung khoang cach den depot.
  // Energy tieu thu ty le voi quang duong, khong phai tai trong.
  // Truoc day dung `distance x demand` lam sai thu tu uu tien.
  std::sort(customers.begin(), customers.end(), [&](int a, int b) {
    double difficultyA = instance->getDistance(0, a);
    double difficultyB = instance->getDistance(0, b);
    return difficultyA > difficultyB; // Hardest first (phat sinh nhieu nang luong nhat)
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

    // FIX #8 + #9: tryStationAssisted tim VI TRI va ROUTE tot nhat, khong phai dau tien.
    // FIX #16: Luon goi evaluate() de bao dam states khong bj stale.
    // Refactor thanh lambda tra ve bool va cap nhat routes neu tim duoc insertion tot hon.
    auto tryStationAssisted = [&]() -> bool {
      // Duyet qua tat ca stations de xay dung sorted list theo detour
      const auto& allStations = instance->getStations();

      double bestCost = std::numeric_limits<double>::infinity();
      int bestRouteIdx = -1;
      std::optional<Route> bestRoute;

      for (int rIdx : feasibleRoutesIndices) {
        // FIX #16: Luon goi evaluate() truoc khi doc states
        routes[rIdx].evaluate();
        const auto &nodes = routes[rIdx].getNodes();
        const double energyRate = routes[rIdx].getVehicle()->getEnergyConsumptionRate();
        const auto  &states     = routes[rIdx].getStates();

        for (size_t pos = 1; pos < nodes.size(); ++pos) {
          int prevId = nodes[pos - 1];

          // FIX #6 (trong scope nay): thu top stations theo detour, khong chi nearest
          // Sap xep cac station theo detour toi pos nay
          struct StDet { int id; double det; };
          std::vector<StDet> stCands;
          for (const auto& st : allStations) {
            int sid = st->getId();
            double det = instance->getDistance(prevId, sid)
                       + instance->getDistance(sid, customerId);
            stCands.push_back({sid, det});
          }
          int topK = std::min(3, (int)stCands.size());
          std::partial_sort(stCands.begin(), stCands.begin() + topK, stCands.end(),
                            [](const StDet& a, const StDet& b){ return a.det < b.det; });

          for (int k = 0; k < topK; ++k) {
            int nearStation = stCands[k].id;

            // Pattern [station, customer]
            {
              double battAtPrev = (pos - 1 < states.size()) ? states[pos - 1].remainingBattery : 0.0;
              double distToSt   = instance->getDistance(prevId, nearStation) * energyRate;
              if (battAtPrev >= distToSt) {
                Route copy1 = routes[rIdx];
                copy1.addNode(nearStation, pos);
                copy1.addNode(customerId, pos + 1);
                copy1.evaluate();
                if (copy1.isFeasible()) {
                  double cost = copy1.getTotalDistance() - routes[rIdx].getTotalDistance();
                  if (cost < bestCost) {
                    bestCost = cost;
                    bestRouteIdx = rIdx;
                    bestRoute = copy1;
                  }
                }
              }
            }
            // Pattern [customer, station]
            {
              double battAtPrev = (pos - 1 < states.size()) ? states[pos - 1].remainingBattery : 0.0;
              double distToCust = instance->getDistance(prevId, customerId) * energyRate;
              if (battAtPrev >= distToCust) {
                Route copy2 = routes[rIdx];
                copy2.addNode(customerId, pos);
                copy2.addNode(nearStation, pos + 1);
                copy2.evaluate();
                if (copy2.isFeasible()) {
                  double cost = copy2.getTotalDistance() - routes[rIdx].getTotalDistance();
                  if (cost < bestCost) {
                    bestCost = cost;
                    bestRouteIdx = rIdx;
                    bestRoute = copy2;
                  }
                }
              }
            }
          }
        }
      }

      if (bestRouteIdx != -1 && bestRoute.has_value()) {
        routes[bestRouteIdx] = bestRoute.value();
        return true;
      }
      return false;
    };

    double minExactCost = std::numeric_limits<double>::max();
    int bestRouteIdx = -1;
    size_t bestPos = -1;

    for (int r : feasibleRoutesIndices) {
      const auto &nodes = routes[r].getNodes();
      for (size_t pos = 1; pos < nodes.size(); ++pos) {

        if (!routes[r].canPossiblyInsert(customerId, pos)) {
          continue;
        }

        auto exactResult = routes[r].checkInsertionCost(customerId, pos);

        if (exactResult.isFeasible) {
          auto slack = routes[r].getEnergySlack();
          double energySlackAtPos = 0.0;
          if (pos > 0 && pos - 1 < slack.size()) {
            energySlackAtPos = slack[pos - 1];
          }

          double battCap = routes[r].getVehicle()->getBatteryCapacity();

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
            energyCost += battCap * 0.05; // ~5% of battery capacity as penalty
          }

          // 4. Bottleneck penalty: scaled by battCap for consistency
          if (pos > 0 && pos - 1 < slack.size()) {
            double threshold   = 0.15 * battCap;
            double localSlack  = slack[pos - 1];
            if (localSlack < threshold) {
              // ⭐ Fix: normalize bottleneckPenalty → ∈ [0, 1], weight calibrated
              double bottleneckPenalty = (threshold - localSlack) / std::max(threshold, 1e-9);
              energyCost += battCap * 0.03 * bottleneckPenalty; // ~3% battCap max
            }
          }

          if (energyCost < minExactCost) {
            minExactCost = energyCost;
            bestRouteIdx = r;
            bestPos = pos;
          }
        }
      }
    }

    if (bestRouteIdx != -1) {
      routes[bestRouteIdx].addNode(customerId, bestPos);
      routes[bestRouteIdx].evaluate();
    } else {
      // FIX #8+#9: tryStationAssisted gio tu duyet tat ca routes/positions
      bool insertedWithStation = tryStationAssisted();
      if (!insertedWithStation) {
        createNewRouteForCustomer(solution, customerId, instance);
      }
    }
  }
}