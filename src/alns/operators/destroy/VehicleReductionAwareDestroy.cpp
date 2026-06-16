// =============================================================================
// VehicleReductionAwareDestroy.cpp — Phase 3 Rewrite
// =============================================================================
// Strategy:
//   1. detectProfile() → determine instance type:
//      TW_SATURATION_BREAK  : tight TW slots, many narrowly packed customers
//      ANCHOR_UNLOCK        : few customers anchor many routes to same time band
//      FORCED_VICTIM_REMOVAL: default — just evict smallest route
//   2. findVictimRoute() → route with fewest customers
//   3. Remove ALL customers from victim, plus profile-guided extras
//   4. Remove empty routes
//   5. Return sorted unserved list (by strategy)
// =============================================================================

#include "../../../../include/alns/operators/destroy/VehicleReductionAwareDestroy.h"
#include "../../../../include/core/Route.h"
#include "../../../../include/core/Solution.h"
#include "../../../../include/core/Instance.h"
#include "../../../../include/core/Node.h"
#include "../../../../include/core/Customer.h"
#include "../../../../include/core/Depot.h"
#include "../../../../include/core/Station.h"
#include <algorithm>
#include <numeric>
#include <random>
#include <climits>
#include <iostream>

VehicleReductionAwareDestroy::VehicleReductionAwareDestroy(std::shared_ptr<Instance> instance)
    : instance_(std::move(instance)) {}

std::string VehicleReductionAwareDestroy::getName() const {
  return "VehicleReductionAwareDestroy";
}

// ─────────────────────────────────────────────────────────────────────────────
// detectProfile
// ─────────────────────────────────────────────────────────────────────────────
VehicleReductionAwareDestroy::InstanceProfile
VehicleReductionAwareDestroy::detectProfile(const Solution& solution) const {
  const auto& customers = instance_->getCustomers();
  if (customers.empty())
    return VehicleReductionAwareDestroy::InstanceProfile::FORCED_VICTIM_REMOVAL;

  // Compute average TW width
  double totalTW = 0.0;
  for (const auto& c : customers)
    totalTW += (c->getDueDate() - c->getReadyTime());
  double avgTW = totalTW / (double)customers.size();

  // Count customers with tight TW (narrower than 30% of average)
  int tightCount = 0;
  for (const auto& c : customers)
    if ((c->getDueDate() - c->getReadyTime()) < avgTW * 0.3) tightCount++;

  double tightRatio = (double)tightCount / (double)customers.size();
  if (tightRatio > 0.25)
    return VehicleReductionAwareDestroy::InstanceProfile::TW_SATURATION_BREAK;

  // Check anchor: routes that share a very narrow ready-time band
  const auto& routes = solution.getRoutes();
  if (!routes.empty()) {
    std::vector<double> routeMinReady;
    for (const auto& r : routes) {
      double minRT = 1e18;
      for (int nodeId : r.getNodes()) {
        if (instance_->getNodeType(nodeId) == NodeType::CUSTOMER) {
          auto c = std::dynamic_pointer_cast<Customer>(instance_->getNodeById(nodeId));
          if (c) minRT = std::min(minRT, c->getReadyTime());
        }
      }
      if (minRT < 1e17) routeMinReady.push_back(minRT);
    }
    if (!routeMinReady.empty()) {
      std::sort(routeMinReady.begin(), routeMinReady.end());
      double band = avgTW / 4.0;
      int bandCount = 0;
      for (double rt : routeMinReady)
        if (std::abs(rt - routeMinReady.front()) <= band) bandCount++;
      if ((double)bandCount / (double)routeMinReady.size() > 0.4)
        return VehicleReductionAwareDestroy::InstanceProfile::ANCHOR_UNLOCK;
    }
  }

  return VehicleReductionAwareDestroy::InstanceProfile::FORCED_VICTIM_REMOVAL;
}

// ─────────────────────────────────────────────────────────────────────────────
// findVictimRoute — returns index of route with fewest customers (≥1)
// ─────────────────────────────────────────────────────────────────────────────
int VehicleReductionAwareDestroy::findVictimRoute(const Solution& solution) const {
  const auto& routes = solution.getRoutes();
  int victimIdx = -1;
  int minCusts = INT_MAX;
  for (int r = 0; r < (int)routes.size(); ++r) {
    int cnt = (int)routes[r].getCustomers().size();
    if (cnt > 0 && cnt < minCusts) {
      minCusts = cnt;
      victimIdx = r;
    }
  }
  return victimIdx;
}

// ─────────────────────────────────────────────────────────────────────────────
// execute
// ─────────────────────────────────────────────────────────────────────────────
std::vector<int> VehicleReductionAwareDestroy::execute(Solution& solution,
                                                        int /*nodesToRemove*/,
                                                        std::mt19937& /*rng*/) {
  if (solution.getNumRoutes() < 2) return {};

  VehicleReductionAwareDestroy::InstanceProfile profile = detectProfile(solution);
  int victimIdx = findVictimRoute(solution);
  if (victimIdx < 0) return {};

  std::vector<int> unserved;
  auto& routes = solution.getRoutes();

  // Step 1: Remove ALL customers from the victim route
  for (int c : routes[victimIdx].getCustomers())
    unserved.push_back(c);

  // Step 2: Profile-guided extras from other routes
  if (profile == VehicleReductionAwareDestroy::InstanceProfile::TW_SATURATION_BREAK) {
    // Remove 1-2 low-FTS customers from tight TW slots that overlap with victim customers
    double vic_minDue = 1e18;
    for (int c : unserved) {
      auto node = std::dynamic_pointer_cast<Customer>(instance_->getNodeById(c));
      if (node) vic_minDue = std::min(vic_minDue, node->getDueDate());
    }
    int extras = 0;
    for (int r = 0; r < (int)routes.size() && extras < 2; ++r) {
      if (r == victimIdx) continue;
      for (int nodeId : routes[r].getNodes()) {
        if (instance_->getNodeType(nodeId) != NodeType::CUSTOMER) continue;
        auto c = std::dynamic_pointer_cast<Customer>(instance_->getNodeById(nodeId));
        if (!c) continue;
        double tw = c->getDueDate() - c->getReadyTime();
        if (tw < 30.0 && c->getDueDate() >= vic_minDue - 30.0) {
          unserved.push_back(nodeId);
          extras++;
          break;
        }
      }
    }
  } else if (profile == VehicleReductionAwareDestroy::InstanceProfile::ANCHOR_UNLOCK) {
    // Remove 1-2 light-demand anchors from over-anchored small routes
    std::vector<std::pair<double, int>> lightAnchors;
    for (int r = 0; r < (int)routes.size(); ++r) {
      if (r == victimIdx) continue;
      if ((int)routes[r].getCustomers().size() > 4) continue;
      for (int nodeId : routes[r].getNodes()) {
        if (instance_->getNodeType(nodeId) != NodeType::CUSTOMER) continue;
        double dem = instance_->getNodeById(nodeId)->getDemand();
        lightAnchors.push_back({dem, nodeId});
      }
    }
    std::sort(lightAnchors.begin(), lightAnchors.end());
    for (int i = 0; i < (int)lightAnchors.size() && i < 2; ++i)
      unserved.push_back(lightAnchors[i].second);
  }
  // FORCED_VICTIM_REMOVAL: only victim customers (already added)

  // Step 3: Remove empty routes from solution
  solution.removeEmptyRoutes();

  // Step 4: Sort unserved list by strategy
  if (profile == VehicleReductionAwareDestroy::InstanceProfile::TW_SATURATION_BREAK) {
    std::sort(unserved.begin(), unserved.end(), [&](int a, int b) {
      auto ca = std::dynamic_pointer_cast<Customer>(instance_->getNodeById(a));
      auto cb = std::dynamic_pointer_cast<Customer>(instance_->getNodeById(b));
      double ra = ca ? ca->getReadyTime() : 0.0;
      double rb = cb ? cb->getReadyTime() : 0.0;
      return ra < rb;
    });
  } else if (profile == VehicleReductionAwareDestroy::InstanceProfile::ANCHOR_UNLOCK) {
    std::sort(unserved.begin(), unserved.end(), [&](int a, int b) {
      return instance_->getNodeById(a)->getDemand() > instance_->getNodeById(b)->getDemand();
    });
  }

  // Deduplicate
  std::sort(unserved.begin(), unserved.end());
  unserved.erase(std::unique(unserved.begin(), unserved.end()), unserved.end());

  return unserved;
}
