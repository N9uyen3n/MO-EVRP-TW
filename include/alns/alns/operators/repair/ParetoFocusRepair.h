#pragma once
#include "../../IOperator.h"
#include "../../../core/Instance.h"
#include <vector>
#include <string>
#include <random>
#include <memory>

/**
 * @brief TOÁN TỬ: XÂY DỰNG TẬP TRUNG MỤC TIÊU (PARETO FOCUS REPAIR)
 * * **Ý TƯỞNG:**
 * - Chọn ngẫu nhiên 1 mục tiêu (Distance, Time, Energy).
 * - Chèn khách hàng vào vị trí làm tăng chi phí của mục tiêu đó ÍT NHẤT.
 * - Bỏ qua sự suy giảm của các mục tiêu còn lại (nhưng vẫn phải Feasible).
 * * **ĐỘ PHỨC TẠP:** O(k * N).
 */
class ParetoFocusRepair : public IRepairOperator {
public:
    explicit ParetoFocusRepair(std::shared_ptr<Instance> inst);

    std::string getName() const override;

    void execute(Solution& solution, const std::vector<int>& unservedCustomers, std::mt19937& rng) override;

private:
    std::shared_ptr<Instance> instance;
};