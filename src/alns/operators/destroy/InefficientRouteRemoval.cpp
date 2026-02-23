#include "../../../../include/alns/operators/destroy/InefficientRouteRemoval.h"
#include "../../../../include/core/Customer.h"
#include <algorithm>
#include <iostream>
#include <numeric>


InefficientRouteRemoval::InefficientRouteRemoval(
    std::shared_ptr<Instance> instance)
    : instance(instance) {}

std::string InefficientRouteRemoval::getName() const {
  return "Inefficient Route Removal";
}

double InefficientRouteRemoval::calculateRouteScore(const Route &route) const {
  int numCustomers = route.getCustomers().size();

  if (numCustomers == 0)
    return -1e9; // Empty route, don't remove unless necessary

  int stationCount = 0;
  const auto &nodes = route.getNodes();
  for (int nodeId : nodes) {
    if (instance->getNodeById(nodeId)->getType() == NodeType::STATION) {
      stationCount++;
    }
  }

  double score = 0.0;

  // Weights for different inefficiency factors
  // 1. FEWEST CUSTOMERS (Primary goal: eliminate small routes)
  // Scale heavily so routes with < 5 customers have massive scores
  const double w_count = 50.0;
  score += w_count * (1.0 / numCustomers);

  // 2. WAIT TIME (Secondary goal: eliminate routes with excessive waiting)
  const double w_wait = 2.0;
  double avgWait = route.getTotalWaitTime() / numCustomers;
  score += w_wait * avgWait;

  // 3. DISTANCE EFFICIENCY (Tertiary: eliminate meandering routes)
  const double w_dist = 0.5;
  double distPerCust = route.getTotalDistance() / numCustomers;
  score += w_dist * distPerCust;

  // 4. STATION OVERHEAD (Quaternary: eliminate routes with too many charges)
  const double w_station = 5.0;
  score += w_station * stationCount;

  return score;
}

std::vector<int> InefficientRouteRemoval::execute(Solution &solution,
                                                  int /*nodesToRemove*/,
                                                  std::mt19937 &rng) {
  std::vector<int> removedCustomers;
  auto &routes = solution.getRoutes();

  if (routes.empty())
    return removedCustomers;

  // Calculate scores for all routes
  std::vector<std::pair<int, double>> routeScores;
  for (size_t i = 0; i < routes.size(); ++i) {
    double score = calculateRouteScore(routes[i]);
    routeScores.push_back({static_cast<int>(i), score});
  }

  // Sort by score descending (Higher score = More inefficient = Better
  // candidate to remove)
  std::sort(routeScores.begin(), routeScores.end(),
            [](const auto &a, const auto &b) { return a.second > b.second; });

  // Randomized selection (Tournament or Roulette can be used, here we use
  // simple top-k randomization) Select one of the top 3 worst routes
  int k = std::min(3, static_cast<int>(routeScores.size()));
  std::uniform_int_distribution<> dist(0, k - 1);
  int selectedIdx = dist(rng);

  int routeToRemoveIdx = routeScores[selectedIdx].first;

  // Collect customers from the removed route
  const auto &nodes = routes[routeToRemoveIdx].getNodes();
  for (int nodeId : nodes) {
    if (instance->getNodeById(nodeId)->getType() == NodeType::CUSTOMER) {
      removedCustomers.push_back(nodeId);
    }
  }

  // Remove the route
  solution.removeRoute(routeToRemoveIdx);

  return removedCustomers;
}
