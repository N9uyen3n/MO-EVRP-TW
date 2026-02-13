#pragma once

#include "../../../alns/IOperator.h"
#include "../../../core/Instance.h"
#include <vector>
#include <memory>
#include <random>

class InefficientRouteRemoval : public IDestroyOperator {
public:
    InefficientRouteRemoval(std::shared_ptr<Instance> instance);

    std::vector<int> execute(Solution& solution, int nodesToRemove, std::mt19937& rng) override;

    std::string getName() const override;

private:
    std::shared_ptr<Instance> instance;

    double calculateRouteScore(const Route& route) const;
};
