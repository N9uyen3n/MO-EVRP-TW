#pragma once
#include "alns/IOperator.h"
#include "core/Instance.h"

class RouteRemoval : public IDestroyOperator {
public:
    explicit RouteRemoval(const std::shared_ptr<const Instance>& instance);

    std::vector<int> execute(Solution& solution, int nodesToRemove, std::mt19937& rng) override;

    std::string getName() const override;

private:
    std::shared_ptr<const Instance> instance;
};
