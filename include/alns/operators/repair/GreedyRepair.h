// include/alns/operators/repair/GreedyRepair.h
#pragma once
#include "alns/IOperator.h"
#include "core/Instance.h" // <-- BẮT BUỘC
#include <limits>
#include <memory> // Cho std::shared_ptr

/**
 * @brief Chèn lại các khách hàng vào vị trí 'rẻ nhất' (delta-cost)
 */
class GreedyRepair : public IRepairOperator {
public:
    // SỬA CONSTRUCTOR: Phải nhận Instance
    explicit GreedyRepair(std::shared_ptr<Instance> instance);

    std::string getName() const override;

    void execute(Solution& solution, const std::vector<int>& unservedCustomers, std::mt19937& rng) override;

private:
    std::shared_ptr<Instance> instance; // <-- Cần để tạo tuyến mới

    /**
     * @brief Cấu trúc để lưu vị trí chèn tốt nhất
     */
    struct Insertion {
        int customerId = -1;
        int routeIndex = -1;
        size_t position = 0;
        double cost = std::numeric_limits<double>::infinity();
        bool isFeasible = false;
    };

    /**
     * @brief Tìm vị trí chèn TỐT NHẤT (dùng delta-cost)
     */
    Insertion findBestInsertion(int customerId, Solution& solution);
};