#include "../../../../include/alns/operators/destroy/RouteMergingDestroy.h"
#include "../../../../include/core/Customer.h"
#include "../../../../include/core/Station.h"
#include <algorithm>
#include <cmath>
#include <limits>

RouteMergingDestroy::RouteMergingDestroy(std::shared_ptr<Instance> instance)
    : instance(instance) {}

std::string RouteMergingDestroy::getName() const {
    return "Route Merging Destroy";
}

std::pair<double, double> RouteMergingDestroy::calculateRouteCentroid(const Route& route) const {
    double sumX = 0.0, sumY = 0.0;
    int customerCount = 0;
    
    for (int nodeId : route.getNodes()) {
        auto node = instance->getNodeById(nodeId);
        if (node->getType() == NodeType::CUSTOMER) {
            sumX += node->getX();
            sumY += node->getY();
            customerCount++;
        }
    }
    
    if (customerCount == 0) return {0.0, 0.0};
    return {sumX / customerCount, sumY / customerCount};
}

double RouteMergingDestroy::euclideanDistance(double x1, double y1, double x2, double y2) const {
    double dx = x2 - x1;
    double dy = y2 - y1;
    return std::sqrt(dx * dx + dy * dy);
}

double RouteMergingDestroy::getTotalDemand(const Route& route) const {
    double totalDemand = 0.0;
    for (int nodeId : route.getNodes()) {
        auto node = instance->getNodeById(nodeId);
        if (auto customer = std::dynamic_pointer_cast<Customer>(node)) {
            totalDemand += customer->getDemand();
        }
    }
    return totalDemand;
}

bool RouteMergingDestroy::canPotentiallyMerge(const Route& route1, const Route& route2) const {
    // Check 1: Capacity constraint (RELAXED for dense instance repacking)
    
    double totalDemand = getTotalDemand(route1) + getTotalDemand(route2);
    if (totalDemand > instance->getVehicleCapacity() * 1.2) {
        return false;
    }
    
    // Check 2: Time window constraint (rough estimate)
    // Nếu tổng thời gian của 2 tuyến quá lớn, khả năng cao không merge được
    double totalTime = route1.getTotalTime() + route2.getTotalTime();
    auto depotNode = instance->getNodeById(0);
    double maxAllowedTime = depotNode->getDueDate();
    
    // Heuristic: Nếu tổng thời gian > 1.5 * maxTime thì khả năng cao không merge được
    if (totalTime > maxAllowedTime * 1.5) {
        return false;
    }
    
    return true;
}

std::vector<int> RouteMergingDestroy::execute(Solution& solution, int nodesToRemove, std::mt19937& rng) {
    std::vector<int> removedCustomers;
    auto& routes = solution.getRoutes();
    
    // Cần ít nhất 2 tuyến để merge
    if (routes.size() < 2) {
        return removedCustomers;
    }
    
    // Tìm cặp tuyến tốt nhất để merge
    int bestRoute1Idx = -1;
    int bestRoute2Idx = -1;
    double minCentroidDistance = std::numeric_limits<double>::max();
    
    for (size_t i = 0; i < routes.size(); ++i) {
        // Bỏ qua tuyến rỗng
        if (routes[i].getNodes().size() <= 2) continue;
        
        auto centroid1 = calculateRouteCentroid(routes[i]);
        
        for (size_t j = i + 1; j < routes.size(); ++j) {
            // Bỏ qua tuyến rỗng
            if (routes[j].getNodes().size() <= 2) continue;
            
            // Kiểm tra khả năng merge
            if (!canPotentiallyMerge(routes[i], routes[j])) {
                continue;
            }
            
            // Tính khoảng cách centroid
            auto centroid2 = calculateRouteCentroid(routes[j]);
            double distance = euclideanDistance(
                centroid1.first, centroid1.second,
                centroid2.first, centroid2.second
            );
            
            // Cập nhật cặp tốt nhất
            if (distance < minCentroidDistance) {
                minCentroidDistance = distance;
                bestRoute1Idx = static_cast<int>(i);
                bestRoute2Idx = static_cast<int>(j);
            }
        }
    }
    
    // Nếu không tìm thấy cặp phù hợp, fallback: chọn 2 tuyến ngẫu nhiên
    if (bestRoute1Idx == -1) {
        std::vector<int> validRoutes;
        for (size_t i = 0; i < routes.size(); ++i) {
            if (routes[i].getNodes().size() > 2) {
                validRoutes.push_back(static_cast<int>(i));
            }
        }
        
        if (validRoutes.size() >= 2) {
            std::shuffle(validRoutes.begin(), validRoutes.end(), rng);
            bestRoute1Idx = validRoutes[0];
            bestRoute2Idx = validRoutes[1];
        } else {
            // Không đủ tuyến để merge
            return removedCustomers;
        }
    }
    
    // Xóa khách hàng từ cả 2 tuyến (theo thứ tự ngược để tránh lỗi index)
    std::vector<int> routesToRemove = {bestRoute1Idx, bestRoute2Idx};
    std::sort(routesToRemove.begin(), routesToRemove.end(), std::greater<int>());
    
    for (int routeIdx : routesToRemove) {
        const auto& nodes = routes[routeIdx].getNodes();
        for (int nodeId : nodes) {
            auto node = instance->getNodeById(nodeId);
            if (node->getType() == NodeType::CUSTOMER) {
                removedCustomers.push_back(nodeId);
            }
        }
        solution.removeRoute(routeIdx);
    }
    
    return removedCustomers;
}
