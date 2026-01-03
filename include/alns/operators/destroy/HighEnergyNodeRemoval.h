#pragma once
#include "../../IOperator.h"
#include "../../../core/Solution.h"
#include "../../../core/Instance.h"
#include <vector>
#include <string>
#include <random>
#include <memory>

class HighEnergyNodeRemoval : public IDestroyOperator {
public:
    explicit HighEnergyNodeRemoval(std::shared_ptr<Instance> instance, int p = 4);

    std::string getName() const override;

    std::vector<int> execute(Solution& solution, int nodesToRemove, std::mt19937& rng) override;

private:
    std::shared_ptr<Instance> instance;
    int p_param; // Tham số Shaw để chọn ngẫu nhiên (tránh tham lam tuyệt đối)
};