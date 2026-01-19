#pragma once
#include "../../IOperator.h"
#include "../../../core/Instance.h"
#include "../../../core/Solution.h"
#include <memory>
#include <string>

/**
 * @brief TOÁN TỬ: CHÈN THAM LAM THEO THỜI GIAN
 * * **Ý TƯỞNG:**
 * - Tìm vị trí chèn làm tăng tổng thời gian (thời gian di chuyển + thời gian chờ) ít nhất.
 * - Ưu tiên các khách hàng có thể được phục vụ sớm trong time window của họ.
 * * **ĐỘ PHỨC TẠP:** O(k * N).
 */
class GreedyTimeInsertion : public IRepairOperator {
private:
    std::shared_ptr<Instance> instance;

public:
    explicit GreedyTimeInsertion(std::shared_ptr<Instance> inst);

    std::string getName() const override;

    void execute(Solution& solution, const std::vector<int>& unservedCustomers, std::mt19937& rng) override;
};
