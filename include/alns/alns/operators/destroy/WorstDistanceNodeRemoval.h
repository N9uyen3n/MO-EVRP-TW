#pragma once
#include "../../IOperator.h"
#include "../../../core/Instance.h"
#include "../../../core/Solution.h"

/**
 * @brief Xóa các khách hàng "tệ" nhất (gây ra nhiều chi phí nhất).
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
     * @brief Tính chi phí (delta) khi xóa 1 node khỏi tuyến.
     */
    double calculateRemovalCost(const Route& route, size_t position);
};