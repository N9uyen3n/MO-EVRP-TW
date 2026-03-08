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

    // Updated signature with context parameters
    double calculateRouteScore(const Route& route, 
                               double avgCustomersPerRoute,
                               double avgDistPerRoute,
                               double avgWaitPerRoute) const;
};
