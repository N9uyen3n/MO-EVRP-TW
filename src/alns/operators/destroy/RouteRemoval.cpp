// src/alns/operators/RouteRemoval.cpp
#include "alns/operators/destroy/RouteRemoval.h"
#include "core/Customer.h"
#include <iostream>

RouteRemoval::RouteRemoval(const std::shared_ptr<const Instance>& instance)
    : instance(instance) {}

std::string RouteRemoval::getName() const {
    return "RouteRemoval";
}

std::vector<int> RouteRemoval::execute(Solution& solution, int nodesToRemove, std::mt19937& rng) {
    std::vector<int> removedCustomers;

    // Chỉ xóa một tuyến nếu có nhiều hơn một tuyến đường
    if (solution.getNumRoutes() <= 1) {
        return removedCustomers; // Trả về vector rỗng
    }

    // Chọn một tuyến đường ngẫu nhiên để xóa
    std::uniform_int_distribution<size_t> dist(0, solution.getNumRoutes() - 1);
    size_t routeIndex = dist(rng);

    const auto& routeToRemove = solution.getRoutes()[routeIndex];

    // Thu thập tất cả khách hàng từ tuyến đường sẽ bị xóa
    for (int nodeId : routeToRemove.getNodes()) {
        const auto& node = instance->getNodeById(nodeId);
        // Chỉ thêm vào danh sách nếu là khách hàng
        if (std::dynamic_pointer_cast<const Customer>(node)) {
            removedCustomers.push_back(nodeId);
        }
    }

    // Xóa tuyến đường khỏi giải pháp
    solution.removeRoute(routeIndex);

    // In ra thông báo (tùy chọn, hữu ích cho việc gỡ lỗi)
    // std::cout << "[DEBUG] RouteRemoval: Removed route " << routeIndex
    //           << " with " << removedCustomers.size() << " customers." << std::endl;

    return removedCustomers;
}
