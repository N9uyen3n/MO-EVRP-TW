// include/alns/IOperator.h
#pragma once
#include "../core/Solution.h"
#include <vector>
#include <random>
#include <string>
#include <memory>

// Lớp cơ sở (Interface)
class IOperator {
public:
    virtual ~IOperator() = default;
    virtual std::string getName() const = 0;
};

// Interface cho toán tử Phá vỡ (Destroy)
class IDestroyOperator : public IOperator {
public:
    /**
     * @brief Phá vỡ nghiệm 'solution' bằng cách xóa một số khách hàng.
     * @param solution Nghiệm bị sửa đổi TRỰC TIẾP.
     * @param nodesToRemove Số lượng khách hàng cần xóa.
     * @param rng Bộ tạo số ngẫu nhiên.
     * @return std::vector<int> ID của các khách hàng đã bị xóa.
     */
    virtual std::vector<int> execute(Solution& solution, int nodesToRemove, std::mt19937& rng) = 0;
};

// Interface cho toán tử Sửa chữa (Repair)
class IRepairOperator : public IOperator {
public:
    /**
     * @brief Sửa chữa nghiệm 'solution' bằng cách chèn lại các khách hàng.
     * @param solution Nghiệm bị sửa đổi TRỰC TIẾP.
     * @param unservedCustomers ID của các khách hàng cần chèn lại.
     * @param rng Bộ tạo số ngẫu nhiên.
     */
    virtual void execute(Solution& solution, const std::vector<int>& unservedCustomers, std::mt19937& rng) = 0;
};