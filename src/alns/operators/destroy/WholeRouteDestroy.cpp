// =============================================================================
// WholeRouteDestroy.cpp
// =============================================================================
// Whole route destruction operator for aggressive vehicle reduction
// =============================================================================

#include "../../../../include/alns/operators/destroy/WholeRouteDestroy.h"
#include <algorithm>
#include <iostream>
#include <random>
#include <climits>
#include "../../../../include/core/Route.h"
#include "../../../../include/core/Solution.h"
#include "../../../../include/core/Instance.h"
#include "../../../../include/core/Node.h"
#include "../../../../include/core/Customer.h"
#include "../../../../include/core/Depot.h"
#include "../../../../include/core/Station.h"

WholeRouteDestroy::WholeRouteDestroy(std::shared_ptr<Instance> instance)
    : instance_(std::move(instance)) {}

std::string WholeRouteDestroy::getName() const {
  return "WholeRouteDestroy";
}

std::vector<int> WholeRouteDestroy::execute(Solution& solution, int /*nodesToRemove*/, std::mt19937& /*rng*/) {
  if (solution.getNumRoutes() < 2) return {};

  // Find the smallest route (or least anchored with r103)
  int victimIdx = findSmallestRouteWithR103Anchor(solution);
  if (victimIdx < 0) return {};

  // Remove ALL customers from the victim route
  std::vector<int> unserved;
  const auto& routes = solution.getRoutes();
  for (int c : routes[victimIdx].getCustomers())
    unserved.push_back(c);

  // Actually remove the route from solution
  solution.removeRoute(victimIdx);

  return unserved;
}

int WholeRouteDestroy::findSmallestRouteWithR103Anchor(const Solution& solution) const {
  const auto& routes = solution.getRoutes();
  int victimIdx = -1;
  int minCusts = INT_MAX;

  // Find route with minimum customers that has least tight-TW anchors
  for (int r = 0; r < (int)routes.size(); ++r) {
    int custCount = (int)routes[r].getCustomers().size();
    if (custCount > 0 && custCount < minCusts) {
      minCusts = custCount;
      victimIdx = r;
    }
  }

  return victimIdx;
}