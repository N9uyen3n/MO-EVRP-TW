#pragma once

#include "alns/IOperator.h"
#include "core/Instance.h"
#include <memory>
#include <random>
#include <string>
#include <vector>

/**
 * @brief Random Removal destroy operator.
 *
 * Selects customers randomly from all routes for removal.
 * Primary purpose: diversification of the search space.
 */
class RandomRemoval : public IDestroyOperator {
public:
 explicit RandomRemoval(std::shared_ptr<Instance> instance);

 std::string getName() const override;
 std::vector<int> execute(Solution &solution, int nodesToRemove,
                          std::mt19937 &rng) override;

 // Diversity tracking
 DestroyOperatorType getDestroyType() const override { return DestroyOperatorType::RANDOM; }

private:
 std::shared_ptr<Instance> instance;
};
