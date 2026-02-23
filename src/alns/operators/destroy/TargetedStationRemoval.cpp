#include "../../../../include/alns/operators/destroy/TargetedStationRemoval.h"

namespace alns {

TargetedStationRemoval::TargetedStationRemoval(
    std::shared_ptr<Instance> instance)
    : instance(instance) {}

std::string TargetedStationRemoval::getName() const {
  return "Targeted Station Removal";
}

std::vector<int> TargetedStationRemoval::execute(Solution &solution,
                                                 int nodesToRemove,
                                                 std::mt19937 &rng) {
  // This destroy operator removes STATIONS, not customers.
  // The return vector is for removed CUSTOMER IDs, so it will be empty here.
  // However, removing stations makes routes dirty/infeasible, forcing Repair
  // operators to fix them.

  // Note: 'nodesToRemove' is usually for customers. Since stations are fewer,
  // we scale it down. Or we just remove a fixed small number/percentage of
  // stations.
  int stationsToRemove = std::max(1, nodesToRemove / 3);

  auto &routes = solution.getRoutes();
  std::vector<StationCandidate> candidates;

  // 1. Identify and score all stations
  for (int r = 0; r < routes.size(); ++r) {
    const auto &nodes = routes[r].getNodes();

    // Skip depot start (0) and end (size-1)
    for (int i = 1; i < nodes.size() - 1; ++i) {
      auto node = instance->getNodeById(nodes[i]);
      if (node->getType() == NodeType::STATION) {
        double score = calculateStationScore(routes[r], i);
        candidates.push_back({r, i, nodes[i], score});
      }
    }
  }

  if (candidates.empty())
    return {};

  // 2. Sort candidates by score descending (worst stations first)
  // Add some randomness to avoid determinism (shuffle top candidates or add
  // noise to score) Let's add noise to score for randomization
  std::uniform_real_distribution<double> noiseDist(0.9, 1.1);
  for (auto &cand : candidates) {
    cand.score *= noiseDist(rng);
  }

  std::sort(candidates.begin(), candidates.end(),
            std::greater<StationCandidate>());

  // 3. Remove top-K worst stations
  // We must remove carefully because removing a node changes indices of
  // subsequent nodes in the same route. Strategy: Group removals by route and
  // remove from end to start to preserve indices.

  int count = 0;
  // Map: Route Index -> List of indices to remove (sorted descending)
  std::vector<std::pair<int, int>> removals; // (routeIdx, nodeIdx)

  for (const auto &cand : candidates) {
    if (count >= stationsToRemove)
      break;

    // Check if this route already has a removal that conflicts or complicates?
    // Simple way: Add to list, then process list carefully.
    removals.push_back({cand.routeIdx, cand.nodeIdx});
    count++;
  }

  // Sort removals: primary by RouteIdx (desc), secondary by NodeIdx (desc)
  // This allows us to process routes independently, and within a route remove
  // from back to front
  std::sort(removals.begin(), removals.end(),
            [](const std::pair<int, int> &a, const std::pair<int, int> &b) {
              if (a.first != b.first)
                return a.first > b.first;
              return a.second > b.second;
            });

  for (const auto &rm : removals) {
    if (rm.first < routes.size()) { // Safety check
      routes[rm.first].removeNode(rm.second);
    }
  }

  // After removing stations, routes might be infeasible.
  // We re-evaluate them.
  for (auto &route : routes) {
    route.evaluate();
  }

  // Return empty vector because we didn't remove any customers
  return {};
}

double TargetedStationRemoval::calculateStationScore(const Route &route,
                                                     int nodeIdx) const {
  double score = 0.0;
  const auto &nodes = route.getNodes();
  const auto &states = route.getStates();

  // Safety checks
  if (nodeIdx >= nodes.size() || nodeIdx >= states.size())
    return 0.0;

  double chargeAmount = states[nodeIdx].chargeAmount;
  double capacity = route.getVehicle()->getBatteryCapacity();

  // 1. Low Usage (Primary reason)
  // If charge amount is very low relative to capacity (< 15%)
  // This means the station stop was barely worth the time
  if (chargeAmount < 0.15 * capacity) {
    // Linearly increasing score as charge drops to 0
    // Max 50 points if charge is 0
    score += 50.0 * (1.0 - chargeAmount / std::max(1.0, 0.15 * capacity));
  }

  // 2. Detour Penalty (Secondary reason)
  // Estimate detour caused by this station
  int prevNodeId = nodes[nodeIdx - 1];
  int nextNodeId = nodes[nodeIdx + 1];
  int stationId = nodes[nodeIdx];

  double distWithStation = instance->getDistance(prevNodeId, stationId) +
                           instance->getDistance(stationId, nextNodeId);
  double distDirect = instance->getDistance(prevNodeId, nextNodeId);

  double detour = distWithStation - distDirect;

  // Normalize detour? Just use raw distance as penalty
  // Assuming max detour is usually < 100 units.
  score += detour * 1.0;

  // 3. Proximity / Redundancy
  // If station is at node index 1 (right after depot) -> mostly useless unless
  // strict constraints
  if (nodeIdx == 1) {
    score += 100.0;
  }

  // If immediately following another station
  // (Route logic usually prevents consecutive identical stations, but distinct
  // stations can happen)
  auto prevNode = instance->getNodeById(prevNodeId);
  if (prevNode->getType() == NodeType::STATION) {
    score += 100.0;
  }

  return score;
}

} // namespace alns
