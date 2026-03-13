#pragma once
#include "../../IOperator.h"
#include "../../../core/Instance.h"
#include "../../../core/Solution.h"
#include <memory>
#include <vector>
#include <string>
#include <random>

/**
 * @brief Route Merging Destroy Operator
 * 
 * Chiến lược: Chọn 2 tuyến "gần nhau" và có khả năng gộp lại thành 1 tuyến,
 * sau đó xóa toàn bộ khách hàng của cả 2 tuyến. Repair Operator sẽ cố gắng
 * chèn lại vào 1 tuyến duy nhất, từ đó giảm tổng số xe.
 * 
 * Tiêu chí chọn cặp tuyến:
 * 1. Khoảng cách centroid gần nhau (spatial proximity)
 * 2. Tổng demand không vượt quá vehicle capacity
 * 3. Tổng thời gian có khả năng nằm trong time window
 */
class RouteMergingDestroy : public IDestroyOperator {
public:
    explicit RouteMergingDestroy(std::shared_ptr<Instance> instance);
    std::string getName() const override;
    std::vector<int> execute(Solution& solution, int nodesToRemove, std::mt19937& rng) override;

private:
    std::shared_ptr<Instance> instance;
    
    /**
     * @brief Tính toán centroid (tâm) của một tuyến
     * @return pair<x, y> tọa độ trung bình của các khách hàng trong tuyến
     */
    std::pair<double, double> calculateRouteCentroid(const Route& route) const;
    
    /**
     * @brief Tính khoảng cách Euclidean giữa 2 điểm
     */
    double euclideanDistance(double x1, double y1, double x2, double y2) const;
    
    /**
     * @brief Kiểm tra xem 2 tuyến có khả năng merge không
     * @return true nếu có thể merge (capacity và time window hợp lý)
     */
    bool canPotentiallyMerge(const Route& route1, const Route& route2) const;
    
    /**
     * @brief Tính tổng demand của một tuyến
     */
    double getTotalDemand(const Route& route) const;
};
