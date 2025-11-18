#pragma once
#include "alns/IOperator.h"
#include "core/Instance.h"

/**
 * @brief Xóa ngẫu nhiên 'nodesToRemove' khách hàng khỏi nghiệm.
 */
class RandomDestroy : public IDestroyOperator {
public:
    RandomDestroy() = default;

    std::string getName() const override;
    
    std::vector<int> execute(Solution& solution, int nodesToRemove, std::mt19937& rng) override;
};