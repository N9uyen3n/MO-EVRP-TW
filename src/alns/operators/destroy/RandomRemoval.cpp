#include "../../../../include/alns/operators/destroy/RandomRemoval.h"
#include <algorithm>
#include <unordered_set>

RandomRemoval::RandomRemoval(std::shared_ptr<Instance> instance)
    : instance(instance) {}

std::string RandomRemoval::getName() const { return "Random Removal"; }

std::vector<int> RandomRemoval::execute(Solution &solution, int nodesToRemove,
                                        std::mt19937 &rng) {
  std::vector<int> removedCustomers;
  auto &routes = solution.getRoutes();

  // 1. Collect all customer IDs across all routes
  std::vector<std::pair<int, int>> customerRouteMap; // (customerId, routeIdx)
  for (int r = 0; r < static_cast<int>(routes.size()); ++r) {
    const auto &nodes = routes[r].getNodes();
    for (size_t i = 1; i < nodes.size() - 1; ++i) {
      if (instance->getNodeById(nodes[i])->getType() == NodeType::CUSTOMER) {
        customerRouteMap.push_back({nodes[i], r});
      }
    }
  }

  if (customerRouteMap.empty())
    return removedCustomers;

  // 2. Shuffle to randomize selection
  std::shuffle(customerRouteMap.begin(), customerRouteMap.end(), rng);

  // 3. Select min(nodesToRemove, total) customers
  int toRemove =
      std::min(nodesToRemove, static_cast<int>(customerRouteMap.size()));
  for (int i = 0; i < toRemove; ++i) {
    removedCustomers.push_back(customerRouteMap[i].first);
  }

  // 4. Remove selected customers from routes
  std::unordered_set<int> removedSet(removedCustomers.begin(),
                                     removedCustomers.end());
  for (auto &route : routes) {
    bool changed = false;
    std::vector<int> newSequence;
    const auto &nodes = route.getNodes();

    newSequence.push_back(nodes[0]); // Depot start

    for (size_t i = 1; i < nodes.size() - 1; ++i) {
      if (removedSet.find(nodes[i]) == removedSet.end()) {
        newSequence.push_back(nodes[i]);
      } else {
        changed = true;
      }
    }
    newSequence.push_back(nodes.back()); // Depot end

    if (changed) {
      route.clear();
      for (size_t i = 1; i < newSequence.size() - 1; ++i) {
        route.addNode(newSequence[i], i);
      }
      route.evaluate();
    }
  }

  return removedCustomers;
}
