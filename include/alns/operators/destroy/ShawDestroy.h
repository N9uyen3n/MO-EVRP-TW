// include/alns/operators/destroy/ShawDestroy.h
#pragma once
#include "alns/IOperator.h"
#include "core/Instance.h"
#include <map> // Cần cho map

/**
 * @brief (ĐÃ SỬA) Xóa các khách hàng "liên quan" (related)
 */
class ShawDestroy : public IDestroyOperator {
public:
    // Trọng số cho các yếu tố liên quan
    struct RelatednessWeights {
        double distanceWeight = 1.0;
        double timeWindowWeight = 0.5;
        double demandWeight = 0.2; // Thêm trọng số cho demand
        double routeWeight = 0.1;
    };

    /**
     * @brief Constructor đã cập nhật
     * @param determinism Tham số 'p' cho việc chọn ngẫu nhiên
     */
    explicit ShawDestroy(std::shared_ptr<Instance> instance,
                         RelatednessWeights weights,
                         int determinism_param = 3); // Thêm determinism

    std::string getName() const override;

    std::vector<int> execute(Solution& solution, int nodesToRemove, std::mt19937& rng) override;

private:
    std::shared_ptr<Instance> instance;
    RelatednessWeights weights;
    int determinism; // Tham số p

    /**
     * @brief (ĐÃ SỬA) Tính độ liên quan (O(1))
     * Nhanh hơn vì dùng map đã được tính toán trước.
     */
    double calculateRelatedness(int cust1_id, int cust2_id,
                                const std::map<int, int>& custToRoute,
                                const std::map<int, double>& custToDemand);
};