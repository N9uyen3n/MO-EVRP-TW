#pragma once

#include "../core/Solution.h"
#include <vector>
#include <stdexcept>
#include <memory>

namespace alns {

/**
 * @brief Quản lý một pool các đối tượng Solution để tái sử dụng.
 * Giảm chi phí cấp phát/hủy bộ nhớ trong vòng lặp chính của ALNS.
 */
class SolutionPool {
public:
    /**
     * @param poolSize Số lượng Solution cần cấp phát trước.
     * @param instance Một shared_ptr đến đối tượng Instance cần thiết cho constructor của Solution.
     */
    SolutionPool(size_t poolSize, std::shared_ptr<Instance> instance);

    /**
     * @brief Lấy một Solution có sẵn từ pool.
     * @return Một tham chiếu đến một Solution không được sử dụng.
     * @throw std::runtime_error nếu tất cả các đối tượng trong pool đang được sử dụng.
     */
    Solution& acquire();

    /**
     * @brief Trả một Solution về lại cho pool để tái sử dụng.
     * @param solution Tham chiếu đến Solution đã được lấy ra từ pool.
     */
    void release(const Solution& solution);

private:
    std::vector<Solution> pool;
    std::vector<bool> inUse;
    std::shared_ptr<Instance> instance; // Giữ lại để khởi tạo Solution
};

} // namespace alns
