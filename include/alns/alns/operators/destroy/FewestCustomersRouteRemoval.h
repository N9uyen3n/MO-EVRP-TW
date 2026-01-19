#pragma once
#include "alns/IOperator.h"
#include "core/Solution.h"
#include "core/Instance.h"
#include <memory>
#include <vector>
#include <string>
#include <random>

class FewestCustomersRouteRemoval : public IDestroyOperator {
public:
    // Sửa: bỏ const ở shared_ptr để khớp với .cpp
    explicit FewestCustomersRouteRemoval(std::shared_ptr<Instance> instance);

    std::vector<int> execute(Solution& solution, int nodesToRemove, std::mt19937& rng) override;

    std::string getName() const override;

private:
    std::shared_ptr<Instance> instance;
};