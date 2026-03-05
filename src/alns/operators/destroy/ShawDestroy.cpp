#include "../../../../include/alns/operators/destroy/ShawDestroy.h"
#include "../../../../include/core/Customer.h"
#include <algorithm>
#include <cmath>
#include <limits>

ShawDestroy::ShawDestroy(std::shared_ptr<Instance> instance,
                         int determinism_param)
    : instance(instance), determinism(determinism_param) {}

std::string ShawDestroy::getName() const { return "Shaw Removal (Related)"; }

std::vector<int> ShawDestroy::execute(Solution &solution, int nodesToRemove,
                                      std::mt19937 &rng) {
  std::vector<int> removed;
  auto &routes = solution.getRoutes();

  // 1. Xây dựng Map hỗ trợ tra cứu nhanh
  std::vector<int> allCustomers;
  std::map<int, int> custToRoute;
  std::map<int, double> custToDemand;

  // Lấy tất cả khách hàng hiện có trong solution
  for (int r = 0; r < routes.size(); ++r) {
    for (int nodeId : routes[r].getNodes()) {
      auto node = instance->getNodeById(nodeId);
      if (auto cust = std::dynamic_pointer_cast<Customer>(node)) {
        allCustomers.push_back(nodeId);
        custToRoute[nodeId] = r;
        custToDemand[nodeId] = cust->getDemand();
      }
    }
  }

  if (allCustomers.empty())
    return removed;

  // 2. Sinh trọng số ngẫu nhiên (AUTO TUNING)
  std::uniform_real_distribution<double> distW(0.1, 1.0);
  double w_dist = distW(rng);
  double w_time = distW(rng);
  double w_demand = distW(rng);
  double w_route = distW(rng) * 0.5;
  double w_station = distW(rng) * 1.5 + 0.5;

  double totalW = w_dist + w_time + w_demand + w_route + w_station;
  w_dist /= totalW;
  w_time /= totalW;
  w_demand /= totalW;
  w_route /= totalW;
  w_station /= totalW;

  // 3. Chọn hạt giống (Seed) Semi-Deterministic
  // Get top K = 5 most constrained customers (tightest TW)
  std::vector<int> sortedCustomers = allCustomers;
  std::sort(sortedCustomers.begin(), sortedCustomers.end(), [&](int a, int b) {
    auto custA = std::dynamic_pointer_cast<Customer>(instance->getNodeById(a));
    auto custB = std::dynamic_pointer_cast<Customer>(instance->getNodeById(b));
    double twA = custA->getDueDate() - custA->getReadyTime();
    double twB = custB->getDueDate() - custB->getReadyTime();
    return twA < twB; // Tighter TW first
  });

  int K = std::min(5, (int)sortedCustomers.size());
  std::uniform_int_distribution<int> topKDist(0, K - 1);
  int seedIdx = topKDist(rng);
  int seedCustId = sortedCustomers[seedIdx];

  removed.push_back(seedCustId);

  // Xóa seed khỏi danh sách khả dụng
  auto itSeed = std::find(allCustomers.begin(), allCustomers.end(), seedCustId);
  if (itSeed != allCustomers.end())
    allCustomers.erase(itSeed);

  // 4. Lặp để tìm các node tương đồng
  while (removed.size() < nodesToRemove && !allCustomers.empty()) {
    // Chọn ngẫu nhiên 1 node từ danh sách ĐÃ bị xóa để làm mốc so sánh
    std::uniform_int_distribution<int> rDist(0, removed.size() - 1);
    int rId = removed[rDist(rng)];

    // Tính độ tương đồng của tất cả node còn lại với rId
    struct RelatedNode {
      int id;
      double rank; // Giá trị relatedness (càng nhỏ càng tốt)
      bool operator<(const RelatedNode &other) const {
        return rank < other.rank;
      }
    };
    std::vector<RelatedNode> candidates;

    for (int cId : allCustomers) {
      double R =
          calculateRelatedness(rId, cId, custToRoute, custToDemand, w_dist,
                               w_time, w_demand, w_route, w_station);
      candidates.push_back({cId, R});
    }

    // Sắp xếp tăng dần (Rank nhỏ nhất -> Tương đồng nhất -> Lên đầu)
    std::sort(candidates.begin(), candidates.end());

    // Chọn ngẫu nhiên có định hướng (Shaw selection)
    std::uniform_real_distribution<double> d(0.0, 1.0);
    int idx =
        static_cast<int>(candidates.size() * std::pow(d(rng), determinism));
    if (idx >= candidates.size())
      idx = candidates.size() - 1;

    int selectedId = candidates[idx].id;
    removed.push_back(selectedId);

    // Xóa khỏi danh sách allCustomers (tìm và xóa)
    auto it = std::find(allCustomers.begin(), allCustomers.end(), selectedId);
    if (it != allCustomers.end())
      allCustomers.erase(it);
  }

  // 5. Thực hiện xóa thực tế trong Solution (Logic Rebuild Route)
  for (auto &route : routes) {
    std::vector<int> newSeq;
    bool changed = false;
    const auto &oldNodes = route.getNodes();

    newSeq.push_back(oldNodes[0]); // Depot
    for (size_t i = 1; i < oldNodes.size() - 1; ++i) {
      bool del = false;
      for (int rm : removed)
        if (rm == oldNodes[i]) {
          del = true;
          break;
        }
      if (!del)
        newSeq.push_back(oldNodes[i]);
      else
        changed = true;
    }
    newSeq.push_back(oldNodes.back()); // Depot

    if (changed) {
      route.clear();
      for (size_t i = 1; i < newSeq.size() - 1; ++i)
        route.addNode(newSeq[i], i);
      route.evaluate();
    }
  }

  return removed;
}

double ShawDestroy::calculateRelatedness(
    int cust1_id, int cust2_id, const std::map<int, int> &custToRoute,
    const std::map<int, double> &custToDemand, double w_dist, double w_time,
    double w_demand, double w_route, double w_station) {

  // Khoảng cách chuẩn hóa (tương đối)
  double dist =
      instance->getDistance(cust1_id, cust2_id) / instance->getMaxDistance();

  // Chênh lệch thời gian phục vụ
  double timeDiff = std::abs(instance->getNodeById(cust1_id)->getReadyTime() -
                             instance->getNodeById(cust2_id)->getReadyTime()) /
                    instance->getMaxTimeWindow();

  // Chênh lệch nhu cầu
  double demandDiff =
      std::abs(custToDemand.at(cust1_id) - custToDemand.at(cust2_id)) /
      instance->getMaxDemand();

  // Khác tuyến hay cùng tuyến
  int routeDiff =
      (custToRoute.at(cust1_id) == custToRoute.at(cust2_id)) ? 0 : 1;

  // ⭐ NEW: Khác trạm sạc gần nhất hay cùng trạm
  // Nếu cùng 1 vùng địa lý (cùng nearest station) -> Relatedness thấp (tốt)
  int s1 = instance->getNearestStationId(cust1_id);
  int s2 = instance->getNearestStationId(cust2_id);
  int stationDiff = (s1 == s2) ? 0 : 1;

  return w_dist * dist + w_time * timeDiff + w_demand * demandDiff +
         w_route * routeDiff + w_station * stationDiff;
}