#include "../../../../include/alns/operators/destroy/InefficientRouteRemoval.h"
#include "../../../../include/core/Customer.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>


InefficientRouteRemoval::InefficientRouteRemoval(
    std::shared_ptr<Instance> instance)
    : instance(instance) {}

std::string InefficientRouteRemoval::getName() const {
  return "Inefficient Route Removal";
}

double InefficientRouteRemoval::calculateRouteScore(
    const Route &route, double avgCustomersPerRoute, double avgDistPerRoute,
    double avgWaitPerRoute) const {

  int numCustomers = route.getCustomers().size();
  if (numCustomers == 0)
    return -1e9;

  double score = 0.0;

  // 1. RELATIVE SIZE: route nhỏ hơn average → dễ eliminate hơn
  double relativeSize = (avgCustomersPerRoute - numCustomers) /
                        std::max(1.0, avgCustomersPerRoute);
  // score += 25.0 * relativeSize;
  score += 45.0 * relativeSize;


  // 2. RELATIVE EFFICIENCY: distance/customer so với average
  // avgDistPerRoute giờ là ratio of means → distPerCust phải tính cùng đơn vị
  double distPerCust = route.getTotalDistance() / numCustomers;
  double relativeEff =
      (distPerCust - avgDistPerRoute) / std::max(1.0, avgDistPerRoute);
  // score += 15.0 * relativeEff;
  score += 7.0 * relativeSize;

  // 3. WAIT TIME: vẫn giữ nhưng relative
  double avgWait = route.getTotalWaitTime() / numCustomers;
  double relativeWait =
      (avgWait - avgWaitPerRoute) / std::max(1.0, avgWaitPerRoute);
  score += 10.0 * relativeWait;

  // 4. BOTTLENECK: soft penalty -> Tăng penalty để trừng phạt route dễ nghẽn
  auto bottlenecks = route.getBottleneckNodes(0.15);
  double bottleneckRatio = (double)bottlenecks.size() / numCustomers;
  score -= std::min(15.0, 25.0 * bottleneckRatio);

  // 5. STATION OVERHEAD
  int stationCount = 0;
  for (int nodeId : route.getNodes())
    if (instance->getNodeById(nodeId)->getType() == NodeType::STATION)
      stationCount++;
  score += 5.0 * ((double)stationCount / numCustomers);

  // 6. RC2-AWARE: MERGABILITY (TW Coverage)
  double twCoverage = 0.0;
  double maxTW = instance->getNodeById(0)->getDueDate();
  for (int custId : route.getCustomers()) {
    auto cust = instance->getNodeById(custId);
    double tw = cust->getDueDate() - cust->getReadyTime();
    twCoverage += tw / std::max(1.0, maxTW);
  }
  double avgTWCoverage = twCoverage / numCustomers;
  score += 20.0 * avgTWCoverage;

  // 7. ⭐ ENERGY PORTABILITY
  // Customers gần depot → dễ redistribute không cần thêm station
  double avgEnergyDemand = 0.0;
  double energyRate = instance->getVehicleEnergyRate();
  for (int custId : route.getCustomers()) {
    double e =
        (instance->getDistance(0, custId) + instance->getDistance(custId, 0)) *
        energyRate;
    avgEnergyDemand += e;
  }
  avgEnergyDemand /= numCustomers;

  double batteryCap = instance->getVehicleBattery();
  double portability = 1.0 - (avgEnergyDemand / std::max(1.0, batteryCap));
  portability = std::max(0.0, portability);

  // ⭐ FIX weight: portability 30 → 15 (tránh bias quá mạnh về near-depot routes)
  // Mục tiêu VR là xóa route nhỏ + không hiệu quả, không phải gần/xa depot.
  // score += 15.0 * portability;
  score += 10.0 * relativeSize;

  return score;
}

std::vector<int> InefficientRouteRemoval::execute(Solution &solution,
                                                  int nodesToRemove,
                                                  std::mt19937 &rng) {
  std::vector<int> removedCustomers;
  auto &routes = solution.getRoutes();

  if (routes.empty())
    return removedCustomers;

  // --- Calculate Context Statistics ---
  int activeRoutes = 0;
  int totalCustomers = 0;
  double totalDistPerCust = 0.0;
  double totalWaitPerCust = 0.0;

  for (const auto &r : routes) {
    if (!r.getCustomers().empty()) {
      activeRoutes++;
      int n = r.getCustomers().size();
      totalCustomers += n;
      // ⭐ FIX: tích lũy tổng distance và tổng customers riêng biệt
      // để tính ratio of means thay vì mean of ratios.
      // Mean of ratios bị bias về routes nhỏ khi kích thước routes chênh lệch nhiều.
      totalDistPerCust += r.getTotalDistance(); // tổng distance (chưa chia n)
      totalWaitPerCust += r.getTotalWaitTime(); // tổng waittime (chưa chia n)
    }
  }

  if (activeRoutes == 0)
    return removedCustomers;

  double avgCust = (double)totalCustomers / activeRoutes;
  // ⭐ FIX: ratio of means = total_dist / total_customers (không phải mean of dist/n)
  double avgDist = (totalCustomers > 0) ? totalDistPerCust / totalCustomers : 1.0;
  double avgWait = (totalCustomers > 0) ? totalWaitPerCust / totalCustomers : 1.0;

  // --- Calculate Scores ---
  std::vector<std::pair<int, double>> routeScores;
  for (size_t i = 0; i < routes.size(); ++i) {
    if (routes[i].getCustomers().empty())
      continue;
    double score = calculateRouteScore(routes[i], avgCust, avgDist, avgWait);
    routeScores.push_back({static_cast<int>(i), score});
  }

  if (routeScores.empty())
    return removedCustomers;

  // --- Determine Target Removal Count ---
  int avgPerRoute = std::max(1, totalCustomers / activeRoutes);
  int targetToRemove = std::max(1, nodesToRemove / avgPerRoute);
  targetToRemove = std::min(targetToRemove, activeRoutes);

  // --- Tournament Selection ---
  std::vector<int> routesToRemove;
  std::vector<bool> selected(routes.size(),
                             false); // Use original route index size

  const int TOURNAMENT_SIZE = 3;
  std::uniform_int_distribution<int> randIdx(0, routeScores.size() - 1);

  while (static_cast<int>(routesToRemove.size()) < targetToRemove &&
         static_cast<int>(routesToRemove.size()) <
             static_cast<int>(routeScores.size())) {

    int bestTournamentIdx = -1;
    double bestTournamentScore = -1e18;

    // ⭐ FIX: sample TOURNAMENT_SIZE distinct candidates (không lặp index)
    // Cũ: randIdx có thể chọn cùng candidate nhiều lần → tournament không đảm bảo diversity
    std::vector<int> sampledIndices;
    int maxTries = TOURNAMENT_SIZE * 4;
    while ((int)sampledIndices.size() < TOURNAMENT_SIZE && maxTries-- > 0) {
      int candidateIdx = randIdx(rng);
      // Chỉ thêm nếu chưa sample lần này
      if (std::find(sampledIndices.begin(), sampledIndices.end(), candidateIdx)
          == sampledIndices.end()) {
        sampledIndices.push_back(candidateIdx);
      }
    }

    for (int candidateIdx : sampledIndices) {
      int routeIdx = routeScores[candidateIdx].first;
      double score = routeScores[candidateIdx].second;
      if (!selected[routeIdx] && score > bestTournamentScore) {
        bestTournamentScore = score;
        bestTournamentIdx   = routeIdx;
      }
    }

    if (bestTournamentIdx != -1) {
      selected[bestTournamentIdx] = true;
      routesToRemove.push_back(bestTournamentIdx);
    } else {
      // Fallback: if tournament fails (e.g. all selected), pick first
      // unselected
      bool found = false;
      for (const auto &p : routeScores) {
        if (!selected[p.first]) {
          selected[p.first] = true;
          routesToRemove.push_back(p.first);
          found = true;
          break;
        }
      }
      if (!found)
        break; // No more unselected routes available
    }
  }

  // Remove chosen routes descending to avoid index shift
  std::sort(routesToRemove.begin(), routesToRemove.end(), std::greater<int>());

  for (int routeIdx : routesToRemove) {
    const auto &nodes = routes[routeIdx].getNodes();
    for (int nodeId : nodes) {
      if (instance->getNodeById(nodeId)->getType() == NodeType::CUSTOMER) {
        removedCustomers.push_back(nodeId);
      }
    }
    solution.removeRoute(routeIdx);
  }

  return removedCustomers;
}