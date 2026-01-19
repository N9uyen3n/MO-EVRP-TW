#include "../../../../include/alns/operators/destroy/LongestWaitTimeRouteRemoval.h"
#include "../../../../include/core/Customer.h" // Cần để check Customer
#include <algorithm>
#include <numeric>

// [SỬA] Constructor nhận Instance
LongestWaitTimeRouteRemoval::LongestWaitTimeRouteRemoval(std::shared_ptr<Instance> instance)
    : instance(instance) {}

std::string LongestWaitTimeRouteRemoval::getName() const {
    return "Longest Wait Time Route Removal";
}

std::vector<int> LongestWaitTimeRouteRemoval::execute(Solution& solution, int /*nodesToRemove*/, std::mt19937& rng) {
    std::vector<int> removedCustomers;
    auto& routes = solution.getRoutes();
    if (routes.empty()) return removedCustomers;

    int bestRouteIdx = -1;
    double maxWaitTime = -1.0;

    // Xáo trộn thứ tự duyệt để ngẫu nhiên hóa nếu có nhiều tuyến wait time bằng nhau
    std::vector<int> indices(routes.size());
    std::iota(indices.begin(), indices.end(), 0);
    std::shuffle(indices.begin(), indices.end(), rng);

    // 1. Tìm tuyến có Wait Time lớn nhất
    for (int idx : indices) {
        // [LƯU Ý] Đảm bảo Route.h có hàm getTotalWaitTime()
        double wt = routes[idx].getTotalWaitTime();
        if (wt > maxWaitTime) {
            maxWaitTime = wt;
            bestRouteIdx = idx;
        }
    }

    // 2. Nếu tìm thấy, thu thập khách hàng và xóa tuyến
    if (bestRouteIdx != -1 && maxWaitTime > 0) {
        const auto& nodes = routes[bestRouteIdx].getNodes();

        // Duyệt qua các node (bỏ qua 2 depot đầu cuối)
        for (size_t i = 1; i < nodes.size() - 1; ++i) {
            int nodeId = nodes[i];

            // [QUAN TRỌNG] Kiểm tra: Chỉ thêm vào danh sách nếu là CUSTOMER
            // Nếu là Station thì cứ để nó bị xóa cùng tuyến, không cần return để chèn lại.
            auto nodePtr = instance->getNodeById(nodeId);
            if (std::dynamic_pointer_cast<Customer>(nodePtr)) {
                removedCustomers.push_back(nodeId);
            }
        }

        // Xóa toàn bộ tuyến khỏi giải pháp
        solution.removeRoute(bestRouteIdx);
    }

    return removedCustomers;
}