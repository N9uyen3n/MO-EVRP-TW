#pragma once
#include "../../IOperator.h"
#include "../../../core/Instance.h"
#include "../../../core/Solution.h"
#include <memory>
#include <vector>
#include <string>
#include <random>

/**
 * @brief Xóa toàn bộ tuyến có ít khách hàng nhất.
 * Enhanced with composite scoring (considers route quality, not just size).
 */
class FewestCustomersRouteRemoval : public IDestroyOperator {
public:
    FewestCustomersRouteRemoval(std::shared_ptr<Instance> instance);
    std::string getName() const override;
    std::vector<int> execute(Solution& solution, int nodesToRemove, std::mt19937& rng) override;

private:
    std::shared_ptr<Instance> instance;
    
    /**
     * @brief Calculate composite removal score for a route
     * Higher score = better candidate for removal
     */
    double calculateRouteRemovalScore(const Route& route, int numCustomers) const;
};