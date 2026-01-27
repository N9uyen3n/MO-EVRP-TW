#pragma once
#include "../../IOperator.h"
#include "../../../core/Instance.h"
#include "../../../core/Solution.h"

/**
 * @brief Xóa các khách hàng "tệ" nhất (gây ra nhiều chi phí nhất).
 * Enhanced version with multi-criteria evaluation (distance, energy, time).
 */
class WorstDistanceNodeRemoval : public IDestroyOperator {
public:
    /**
     * @param determinism Bậc ngẫu nhiên hóa.
     * p=1: Luôn chọn cái tệ nhất.
     * p=5: Tăng xác suất chọn cái tệ nhất, nhưng vẫn có thể chọn cái tệ thứ 2, 3...
     */
    WorstDistanceNodeRemoval(std::shared_ptr<Instance> instance, int determinism_param = 3);

    std::string getName() const override;
    
    std::vector<int> execute(Solution& solution, int nodesToRemove, std::mt19937& rng) override;

private:
    std::shared_ptr<Instance> instance;
    int determinism; // Tham số p

    /**
     * @brief Structure to hold multi-criteria removal impact
     */
    struct RemovalImpact {
        double distanceSaving = 0.0;
        double energySaving = 0.0;
        double timeReduction = 0.0;
        double overallCost = 0.0;
    };

    /**
     * @brief Calculate multi-criteria removal impact
     */
    RemovalImpact calculateRemovalImpact(const Route& route, size_t position);
    
    /**
     * @brief Calculate route quality bonus (higher for worse routes)
     */
    double getRouteQualityBonus(const Route& route);
    
    /**
     * @brief Calculate time window slack (higher = easier to reinsert)
     */
    double getTimeWindowSlack(int customerId, const Route& route);
    
    /**
     * @brief Legacy method for backward compatibility
     */
    double calculateRemovalCost(const Route& route, size_t position);
};
