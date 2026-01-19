#pragma once
#include "alns/IOperator.h"
#include "core/Solution.h"
#include "core/Instance.h"
#include <string>
#include <vector>
#include <memory>

class LongestWaitTimeRouteRemoval : public IDestroyOperator {
public:
    explicit LongestWaitTimeRouteRemoval(std::shared_ptr<Instance> instance);

    std::string getName() const override; // Chỉ khai báo, không định nghĩa ở đây

    std::vector<int> execute(Solution& solution, int nodesToRemove, std::mt19937& rng) override;

private:
    std::shared_ptr<Instance> instance;
};