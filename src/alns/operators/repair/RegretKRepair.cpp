#include "../../../../include/alns/operators/repair/RegretKRepair.h"
#include "../../../../include/core/Customer.h"
#include "../../../../include/core/Vehicle.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

// ============================================================================
// RegretKRepair — Redesign
// ============================================================================
// ============================================================================

RegretKRepair::RegretKRepair(std::shared_ptr<Instance> instance, int k,
                             double noiseParameter)
    : instance(instance), k_regret(k), noiseParam(noiseParameter) {
  // Cache weight hint (default: pure distance)
  wDist_ = 1.0;
  wGini_ = 0.0;
  wTime_ = 0.0;
}

std::string RegretKRepair::getName() const {
  return "Regret-" + std::to_string(k_regret) + " Repair";
}

void RegretKRepair::setWeightHint(double wDist, double wGini, double wTime) {
  wDist_ = wDist;
  wGini_ = wGini;
  wTime_ = wTime;
}

// ── Helper: tạo route mới cho 1 customer ────────────────────────────────────
namespace {
void createNewRoute(Solution &solution, int custId,
                    std::shared_ptr<Instance> instance) {
  int newId = solution.getNumRoutes();
  auto vehicle = std::make_shared<Vehicle>(
      newId, instance->getVehicleCapacity(), instance->getVehicleBattery(),
      instance->getVehicleEnergyRate());
  Route r(newId, vehicle, instance);
  r.addNode(custId, 1);
  r.evaluate();
  solution.addRoute(r);
}
} // namespace

// ── findKBestInsertions: tìm top-K insertions vào EXISTING routes ────────────
// Trả về sorted vector (best first). KHÔNG bao gồm new-route option — đó là
// fallback riêng của execute().
std::vector<RegretKRepair::InsertionCost>
RegretKRepair::findKBestInsertions(int custId, Solution &solution,
                                   std::mt19937 &rng, int K) {
  auto &routes = solution.getRoutes();
  const double vehCap = instance->getVehicleCapacity();
  std::vector<InsertionCost> results;

  for (int r = 0; r < (int)routes.size(); ++r) {
    const double dem = instance->getNodeById(custId)->getDemand();
    if (routes[r].getTotalDemand() + dem > vehCap)
      continue;

    for (size_t pos = 1; pos < routes[r].getNodes().size(); ++pos) {
      // Tier 1: O(1) pre-filter
      if (!routes[r].canPossiblyInsert(custId, pos))
        continue;

      // Tier 2: Exact evaluation (bỏ fastForwardCheck vì bug timeWait+1.1x)
      InsertionResult res = routes[r].checkInsertionCost(custId, pos);
      if (!res.isFeasible)
        continue;

      // ── Cost formula aligned với ALNS objective ──────────────────────────
      // Distance component (chính)
      double cost = wDist_ * res.deltaDistance;

      // TW penalty: wait time tăng → các node sau bị ép chờ → tăng total time
      double twPenalty = std::max(0.0, res.deltaWaitTime);
      cost += wTime_ * twPenalty;

      // ⭐ Fix: wGini_ dùng để penalize charge amount tăng (energy balance metric)
      // deltaChargeAmount > 0 → route cần sạc nhiều hơn → ít balanced hơn
      if (wGini_ > 0.0) {
        cost += wGini_ * std::max(0.0, res.deltaChargeAmount);
      }

      // Noise để diversification (giữ nhưng chỉ apply lên distance component)
      if (noiseParam > 0.0) {
        std::uniform_real_distribution<double> nd(-noiseParam, noiseParam);
        cost *= std::max(0.1, 1.0 + nd(rng));
      }
      cost = std::max(0.0, cost);

      results.push_back({r, (int)pos, cost, true});
    }
  }

  if (results.empty())
    return {};

  // Partial sort: chỉ cần top-K
  int topK = std::min(K, (int)results.size());
  std::partial_sort(
      results.begin(), results.begin() + topK, results.end(),
      [](const InsertionCost &a, const InsertionCost &b) {
        return a.cost < b.cost;
      });
  results.resize(topK);
  return results;
}

// ── execute() ────────────────────────────────────────────────────────────────
void RegretKRepair::execute(Solution &solution,
                            const std::vector<int> &unservedCustomers,
                            std::mt19937 &rng) {
  std::vector<int> remaining = unservedCustomers;

  while (!remaining.empty()) {
    int currentRoutes = solution.getNumRoutes();

    // Adaptive K: tăng khi còn ít customers (cần chính xác hơn)
    int K;
    int curRem = (int)remaining.size();
    if (curRem > 20)
      K = std::min(3, currentRoutes);
    else if (curRem <= 5)
      K = std::min(std::max(3, k_regret + 1), currentRoutes);
    else
      K = std::min(k_regret, currentRoutes);
    K = std::max(1, K);

    int bestCustId    = -1;
    int bestRouteIdx  = -1;
    int bestPos       = -1;
    double maxRegret  = -1e18;

    for (int custId : remaining) {
      std::vector<InsertionCost> kBest = findKBestInsertions(custId, solution, rng, K);

      double regret;
      int chosenRoute = -1;
      int chosenPos   = -1;

      if (kBest.empty()) {
        // Customer này buộc phải mở route mới → urgent nhất
        // Xử lý trước để các customer khác có thể fill vào route mới đó
        regret    = std::numeric_limits<double>::max() / 2.0;
        chosenRoute = -1;
        chosenPos   = 1;
      } else {
        chosenRoute = kBest[0].routeIndex;
        chosenPos   = kBest[0].position;

        if (kBest.size() == 1) {
          // Chỉ 1 slot khả thi trong toàn bộ solution → cực kỳ urgent
          regret = std::numeric_limits<double>::max() / 2.0;
        } else {
          // Regret = tổng chênh lệch giữa best và rank-2..K
          // KHÔNG bao gồm new-route cost (tránh distortion)
          regret = 0.0;
          for (int i = 1; i < (int)kBest.size(); ++i)
            regret += (kBest[i].cost - kBest[0].cost);
        }

        // Bonus urgency nếu K slots không đủ (solution đang tight)
        if ((int)kBest.size() < K)
          regret += 1e4 * (K - (int)kBest.size());
      }

      if (regret > maxRegret) {
        maxRegret    = regret;
        bestCustId   = custId;
        bestRouteIdx = chosenRoute;
        bestPos      = chosenPos;
      }
    }

    if (bestCustId == -1) {
      // Safety fallback (không nên xảy ra)
      createNewRoute(solution, remaining[0], instance);
      remaining.erase(remaining.begin());
      continue;
    }

    // Apply best insertion
    if (bestRouteIdx == -1) {
      // Không có existing route nào → tạo mới
      createNewRoute(solution, bestCustId, instance);
    } else {
      solution.getRoutes()[bestRouteIdx].addNode(bestCustId, bestPos);
      solution.getRoutes()[bestRouteIdx].evaluate();
    }

    remaining.erase(
        std::remove(remaining.begin(), remaining.end(), bestCustId),
        remaining.end());
  }
}