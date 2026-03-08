#include "../../../../include/alns/operators/destroy/RandomRouteRemoval.h"
#include <algorithm>
#include <vector>

RandomRouteRemoval::RandomRouteRemoval(std::shared_ptr<Instance> instance)
    : instance(instance) {}

std::string RandomRouteRemoval::getName() const {
  return "Random Route Removal";
}

std::vector<int> RandomRouteRemoval::execute(Solution &solution,
                                             int nodesToRemove,
                                             std::mt19937 &rng) {
  // 1. Chọn route để xóa
  std::vector<size_t> candidates;
  const auto &routes = solution.getRoutes();
  for (size_t r = 0; r < routes.size(); ++r) {
    if (!routes[r].getCustomers().empty()) {
      candidates.push_back(r);
    }
  }

  if (candidates.empty()) {
    return {};
  }

  // 2. Weighted random: route nhỏ hơn có xác suất được chọn cao hơn
  std::vector<double> weights;
  for (size_t r : candidates) {
    weights.push_back(1.0 /
                      static_cast<double>(routes[r].getCustomers().size()));
  }

  std::discrete_distribution<size_t> dist(weights.begin(), weights.end());
  size_t selectedCandidateIdx = dist(rng);
  size_t selectedRouteIdx = candidates[selectedCandidateIdx];

  // 3. Extract customers
  std::vector<int> unserved = routes[selectedRouteIdx].getCustomers();

  // 4. Xóa route khỏi solution
  solution.removeRoute(selectedRouteIdx);

  return unserved;
}
