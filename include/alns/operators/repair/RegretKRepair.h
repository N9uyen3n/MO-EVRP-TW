// include/alns/operators/repair/RegretKRepair.h
#pragma once
#include "../../IOperator.h"
#include "../../../core/Instance.h"
#include "../../../core/Solution.h"
#include <limits>
#include <vector>
#include <map>
#include <memory> // Cho std::shared_ptr

/**
 * @brief Chèn lại khách hàng dựa trên "chi phí hối tiếc" (regret cost).
 */
class RegretKRepair : public IRepairOperator {
public:
    /**
     * @brief SỬA CONSTRUCTOR: Phải nhận Instance
     * @param k Tham số k-regret (từ ALNSConfig.regretK)
     * @param noiseParameter Tham số nhiễu (từ ALNSConfig.noiseParameter)
     */
    explicit RegretKRepair(std::shared_ptr<Instance> instance,
                           int k, double noiseParameter);

    std::string getName() const override;

    void execute(Solution& solution, const std::vector<int>& unservedCustomers, std::mt19937& rng);

private:
    std::shared_ptr<Instance> instance; // <-- THÊM VÀO: Cần để tạo tuyến mới
    int k_regret;   // Tham số 'k'
    double noiseParam; // Tham số 'NRR'

    struct InsertionCost {
        int routeIndex;
        size_t position;
        double cost; // Chi phí đa mục tiêu (weighted sum)
        bool isFeasible;
    };

    /**
     * @brief (ĐÃ SỬA) Tính 'k' vị trí chèn tốt nhất (dùng delta-cost)
     */
    std::vector<InsertionCost> findKBestInsertions(int customerId, Solution& solution, std::mt19937& rng);
};