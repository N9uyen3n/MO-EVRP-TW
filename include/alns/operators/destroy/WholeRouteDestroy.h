// =============================================================================
// WholeRouteDestroy.h
// =============================================================================

#pragma once
#include <vector>
#include <memory>
#include <random>
#include "../../IOperator.h"

class Solution;
class Instance;

class WholeRouteDestroy : public IDestroyOperator {
public:
  explicit WholeRouteDestroy(std::shared_ptr<Instance> instance);
  std::string getName() const override;
  std::vector<int> execute(Solution& solution, int nodesToRemove, std::mt19937& rng) override;

private:
  int findSmallestRouteWithR103Anchor(const Solution& solution) const;
  std::shared_ptr<Instance> instance_;
};