#include "../../../../include/alns/operators/destroy/FewestCustomersRouteRemoval.h"
#include <algorithm>
#include <limits>
#include <numeric> // iota

FewestCustomersRouteRemoval::FewestCustomersRouteRemoval(std::shared_ptr<Instance> instance)
    : instance(instance) {}

std::string FewestCustomersRouteRemoval::getName() const{
    return "Fewest Customers Route Removal";
}

std::vector<int> FewestCustomersRouteRemoval::execute(Solution& solution, int /*nodesToRemove*/, std::mt19937& rng) {
    std::vector<int> removedCustomers;
    auto& routes = solution.getRoutes();
    if (routes.empty()) return removedCustomers;

    int bestRouteIdx = -1;
    size_t minCustomers = std::numeric_limits<size_t>::max();

    // Randomize thứ tự duyệt để tránh thiên vị
    std::vector<int> indices(routes.size());
    std::iota(indices.begin(), indices.end(), 0);
    std::shuffle(indices.begin(), indices.end(), rng);

    // Tìm tuyến có ít khách nhất
    for (int idx : indices) {
        const auto& nodes = routes[idx].getNodes();
        size_t count = 0;
        // Đếm số lượng Customer (bỏ Depot và Station)
        for(int nodeId : nodes) {
            if (std::dynamic_pointer_cast<Customer>(instance->getNodeById(nodeId))) {
                count++;
            }
        }

        // Nếu tuyến rỗng (chỉ có depot) cũng coi là 0 khách
        if (count > 0 && count < minCustomers) {
            minCustomers = count;
            bestRouteIdx = idx;
        }
    }

    // Xóa tuyến
    if (bestRouteIdx != -1) {
        const auto& nodes = routes[bestRouteIdx].getNodes();
        for (int nodeId : nodes) {
            if (std::dynamic_pointer_cast<Customer>(instance->getNodeById(nodeId))) {
                removedCustomers.push_back(nodeId);
            }
        }
        solution.removeRoute(bestRouteIdx);
    }

    return removedCustomers;
}