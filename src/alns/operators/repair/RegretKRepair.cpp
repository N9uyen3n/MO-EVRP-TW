#include "../../../../include/alns/operators/repair/RegretKRepair.h"
#include "../../../../include/core/Customer.h"
#include "../../../../include/core/Vehicle.h"
#include <algorithm>
#include <limits>
#include <vector>

// ============================================================================
// RegretKRepair — SMART PACKING & FLEET REDUCTION EDITION
// ============================================================================

RegretKRepair::RegretKRepair(std::shared_ptr<Instance> instance, int k,
                             double noiseParameter)
    : instance(instance), k_regret(k), noiseParam(noiseParameter) {
  // Chỉ giữ lại trọng số quãng đường (wDist_), Gini và Time đã bị loại bỏ
  wDist_ = 1.0;
}

std::string RegretKRepair::getName() const {
  return "Regret-" + std::to_string(k_regret) + " Repair (Smart Packing)";
}

void RegretKRepair::setWeightHint(double wDist, double /*wGini*/, double /*wTime*/) {
  wDist_ = wDist;
}

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

std::vector<RegretKRepair::InsertionCost>
RegretKRepair::findKBestInsertions(int custId, Solution &solution,
                                   std::mt19937 &rng, int K) {
  auto &routes = solution.getRoutes();
  const double vehCap = instance->getVehicleCapacity();
  std::vector<InsertionCost> results;

  double dem = instance->getNodeById(custId)->getDemand();

  for (int r = 0; r < (int)routes.size(); ++r) {
    double currentDem = routes[r].getTotalDemand();
    if (currentDem + dem > vehCap)
      continue;

    // [THÔNG MINH 1]: KHUYẾN KHÍCH NHỒI CHẶT (Packing Bonus)
    // Tỷ lệ lấp đầy càng cao, điểm cost càng được TRỪ đi nhiều.
    // Thuật toán sẽ ưu tiên nhét khách vào xe đã đầy 90% thay vì xe đang trống 50%
    double fillRate = (currentDem + dem) / vehCap;
    double packingBonus = 2000.0 * fillRate;

    for (size_t pos = 1; pos < routes[r].getNodes().size(); ++pos) {
      if (!routes[r].canPossiblyInsert(custId, pos))
        continue;

      InsertionResult res = routes[r].checkInsertionCost(custId, pos);
      if (!res.isFeasible)
        continue;

      // Chi phí đơn thuần = Khoảng cách tăng thêm - Điểm thưởng nhồi nhét
      double cost = (wDist_ * res.deltaDistance) - packingBonus;

      if (noiseParam > 0.0) {
        std::uniform_real_distribution<double> nd(0.9, 1.1); // Noise biên độ hẹp
        cost *= nd(rng);
      }

      results.push_back({r, (int)pos, cost, true});
    }
  }

  if (results.empty()) return {};

  int topK = std::min(K, (int)results.size());
  std::partial_sort(
      results.begin(), results.begin() + topK, results.end(),
      [](const InsertionCost &a, const InsertionCost &b) {
        return a.cost < b.cost; // Cost âm càng sâu càng tốt
      });
  results.resize(topK);
  return results;
}

void RegretKRepair::execute(Solution &solution,
                            const std::vector<int> &unservedCustomers,
                            std::mt19937 &rng) {
  std::vector<int> remaining = unservedCustomers;
  const double URGENCY_PENALTY = 100000.0;

  while (!remaining.empty()) {
    int currentRoutes = solution.getNumRoutes();
    int curRem = (int)remaining.size();

    // Adaptive K
    int K = (curRem <= 5) ? std::min(std::max(3, k_regret + 1), currentRoutes)
                          : std::min(k_regret, currentRoutes);
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
        // [THÔNG MINH 2]: TRÌ HOÃN XE MỚI TỚI CÙNG
        // Khách này hiện không có chỗ? Gán regret cực thấp để đẩy xuống cuối hàng.
        // Chờ các khách khác xếp xong, cấu trúc thay đổi biết đâu lại có chỗ.
        regret = -1e9;
      } else {
        chosenRoute = kBest[0].routeIndex;
        chosenPos   = kBest[0].position;

        if (kBest.size() == 1) {
          // [THÔNG MINH 3]: BÁO ĐỘNG ĐỎ
          // Khách chỉ còn ĐÚNG 1 vị trí nhét được trên toàn hệ thống.
          // Ép Regret lên cực cao để nhét ngay lập tức, tránh bị khách khác nẫng mất.
          regret = URGENCY_PENALTY - kBest[0].cost;
        } else {
          // Standard Regret = Sum(Cost_i - Cost_best)
          regret = 0.0;
          for (int i = 1; i < (int)kBest.size(); ++i) {
            regret += (kBest[i].cost - kBest[0].cost);
          }
          // Phạt nhẹ nếu số lượng vị trí dự phòng < K
          if ((int)kBest.size() < K) {
             regret += (K - kBest.size()) * 500.0;
          }
        }
      }

      if (regret > maxRegret) {
        maxRegret    = regret;
        bestCustId   = custId;
        bestRouteIdx = chosenRoute;
        bestPos      = chosenPos;
      }
    }

    if (bestRouteIdx == -1) {
      // Bất đắc dĩ: Toàn bộ khách hàng đều vô phương cứu chữa mới phải sinh xe mới.
      createNewRoute(solution, bestCustId, instance);
    } else {
      solution.getRoutes()[bestRouteIdx].addNode(bestCustId, bestPos);
      solution.getRoutes()[bestRouteIdx].evaluate();
    }

    // Xóa khách hàng đã xử lý bằng Swap-and-Pop O(1)
    auto it = std::find(remaining.begin(), remaining.end(), bestCustId);
    if (it != remaining.end()) {
        *it = remaining.back();
        remaining.pop_back();
    }
  }
}