#pragma once
#include "../../IOperator.h"
#include "../../../core/Instance.h" // Đường dẫn tùy cấu trúc thực tế
#include <vector>
#include <string>
#include <random>
#include <memory>

/**
 * @brief TOÁN TỬ: PHÁ HỦY TẬP TRUNG MỤC TIÊU (PARETO FOCUS DESTROY)
 * * **Ý TƯỞNG:**
 * - Mỗi lần chạy, chọn ngẫu nhiên 1 trong 3 mục tiêu (Distance, Time, Energy).
 * - Tìm các node gây ra chi phí lớn nhất *riêng cho mục tiêu đó* để xóa.
 * * **ĐỘ PHỨC TẠP:** O(N log N).
 */
class ParetoFocusDestroy : public IDestroyOperator {
public:
    explicit ParetoFocusDestroy(std::shared_ptr<Instance> inst, int p = 4);

    std::string getName() const override;

    std::vector<int> execute(Solution& solution, int nodesToRemove, std::mt19937& rng) override;

private:
    std::shared_ptr<Instance> instance;
    int p_param; // Tham số ngẫu nhiên hóa (Shaw parameter)
};