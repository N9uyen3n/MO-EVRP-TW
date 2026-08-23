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
#include <unordered_set>
#include <cassert>

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
std::vector<int> VehicleReductionAwareDestroy::execute(
    Solution& solution,
    int /*nodesToRemove*/,
    std::mt19937& /*rng*/) {

  if (solution.getNumRoutes() < 2)
    return {};

  const InstanceProfile profile = detectProfile(solution);
  const int victimIdx = findVictimRoute(solution);

  if (victimIdx < 0)
    return {};

  auto& routes = solution.getRoutes();

  std::vector<int> unserved;
  std::unordered_set<int> selected;

  // Insert once while preserving the selection order.
  auto addUnserved = [&](int customerId) {
    if (selected.insert(customerId).second)
      unserved.push_back(customerId);
  };

  // -------------------------------------------------------------------------
  // Phase 1: Collect all customers from the victim route.
  // -------------------------------------------------------------------------
  const std::vector<int> victimCustomers = routes[victimIdx].getCustomers();

  for (int customerId : victimCustomers)
    addUnserved(customerId);

  // -------------------------------------------------------------------------
  // Phase 2: Profile-guided bottleneck customers from other routes.
  // -------------------------------------------------------------------------
  if (profile == InstanceProfile::TW_SATURATION_BREAK) {

    int extras = 0;

    for (int r = 0;
         r < static_cast<int>(routes.size()) && extras < 2;
         ++r) {

      if (r == victimIdx) continue;

      for (int nodeId : routes[r].getNodes()) {

        if (instance_->getNodeType(nodeId) != NodeType::CUSTOMER)
          continue;

        auto candidate = std::dynamic_pointer_cast<Customer>(
            instance_->getNodeById(nodeId));

        if (!candidate) continue;

        const double candidateWidth =
            candidate->getDueDate() - candidate->getReadyTime();

        if (candidateWidth >= 30.0) continue;

        // Require actual time-window overlap with victim route customers.
        bool overlapsVictim = false;
        for (int victimId : victimCustomers) {
          auto victim = std::dynamic_pointer_cast<Customer>(
              instance_->getNodeById(victimId));
          if (!victim) continue;
          const double overlapStart =
              std::max(candidate->getReadyTime(), victim->getReadyTime());
          const double overlapEnd =
              std::min(candidate->getDueDate(), victim->getDueDate());
          if (overlapStart <= overlapEnd) {
            overlapsVictim = true;
            break;
          }
        }

        if (overlapsVictim) {
          addUnserved(nodeId);
          ++extras;
          break; // At most one extra customer from this route.
        }
      }
    }

  } else if (profile == InstanceProfile::ANCHOR_UNLOCK) {

    std::vector<std::pair<double, int>> lightAnchors;

    for (int r = 0; r < static_cast<int>(routes.size()); ++r) {
      if (r == victimIdx) continue;
      if (static_cast<int>(routes[r].getCustomers().size()) > 4) continue;
      for (int nodeId : routes[r].getNodes()) {
        if (instance_->getNodeType(nodeId) != NodeType::CUSTOMER) continue;
        const double demand = instance_->getNodeById(nodeId)->getDemand();
        lightAnchors.emplace_back(demand, nodeId);
      }
    }

    std::sort(
        lightAnchors.begin(), lightAnchors.end(),
        [](const auto& lhs, const auto& rhs) {
          if (lhs.first != rhs.first) return lhs.first < rhs.first;
          return lhs.second < rhs.second;
        });

    for (int i = 0;
         i < static_cast<int>(lightAnchors.size()) && i < 2;
         ++i) {
      addUnserved(lightAnchors[i].second);
    }
  }
  // FORCED_VICTIM_REMOVAL: only victim-route customers.

  // -------------------------------------------------------------------------
  // Phase 3: Actually remove every selected customer from the solution.
  // Iterate backward because removeNode() shifts the sequence.
  // -------------------------------------------------------------------------
  for (Route& route : routes) {
    for (int pos = static_cast<int>(route.getNodes().size()) - 1;
         pos >= 0;
         --pos) {
      const int nodeId = route.getNodes()[pos];
      if (instance_->getNodeType(nodeId) == NodeType::CUSTOMER &&
          selected.count(nodeId) > 0) {
        route.removeNode(static_cast<std::size_t>(pos));
      }
    }
  }

  // Victim route is now empty; remove it.
  solution.removeEmptyRoutes();

  // -------------------------------------------------------------------------
  // Phase 4: Sort unserved list according to detected profile.
  // Must be after deduplication; numeric sort would destroy profile ordering.
  // -------------------------------------------------------------------------
  if (profile == InstanceProfile::TW_SATURATION_BREAK) {
    std::sort(
        unserved.begin(), unserved.end(),
        [&](int lhsId, int rhsId) {
          auto lhs = std::dynamic_pointer_cast<Customer>(
              instance_->getNodeById(lhsId));
          auto rhs = std::dynamic_pointer_cast<Customer>(
              instance_->getNodeById(rhsId));
          const double lhsReady = lhs ? lhs->getReadyTime() : 0.0;
          const double rhsReady = rhs ? rhs->getReadyTime() : 0.0;
          if (lhsReady != rhsReady) return lhsReady < rhsReady;
          return lhsId < rhsId;
        });
  } else if (profile == InstanceProfile::ANCHOR_UNLOCK) {
    std::sort(
        unserved.begin(), unserved.end(),
        [&](int lhsId, int rhsId) {
          const double lhsDemand =
              instance_->getNodeById(lhsId)->getDemand();
          const double rhsDemand =
              instance_->getNodeById(rhsId)->getDemand();
          if (lhsDemand != rhsDemand) return lhsDemand > rhsDemand;
          return lhsId < rhsId;
        });
  }

#ifndef NDEBUG
  // Destroy contract: every returned customer must be absent from solution.
  for (const Route& route : solution.getRoutes()) {
    for (int nodeId : route.getNodes()) {
      if (instance_->getNodeType(nodeId) == NodeType::CUSTOMER)
        assert(selected.count(nodeId) == 0);
    }
  }
#endif

  return unserved;
}
