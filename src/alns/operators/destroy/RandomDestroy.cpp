// src/alns/operators/destroy/RandomDestroy.cpp

#include "alns/operators/destroy/RandomDestroy.h"
#include <algorithm> // for std::shuffle, std::sort
#include <vector>
#include <tuple>

std::string RandomDestroy::getName() const {
    return "Random Destroy";
}

struct CandidateRemoval {
    int routeIndex;
    int nodeIndex;
    int customerId;
};

std::vector<int> RandomDestroy::execute(Solution& solution, int nodesToRemove, std::mt19937& rng) {

    std::vector<CandidateRemoval> customers;
    // Dự trữ bộ nhớ để tránh cấp phát lại nhiều lần
    customers.reserve(solution.getNumRoutes() * 10);

    // 1. Thu thập tất cả khách hàng và vị trí của họ
    const auto& routes = solution.getRoutes();
    for (size_t r = 0; r < routes.size(); ++r) {
        const auto& nodes = routes[r].getNodes();
        // Bỏ qua Depot (0) và node cuối, duyệt từ 1 đến size-2
        for (size_t n = 1; n < nodes.size() - 1; ++n) {
            int id = nodes[n];
            // Giả định ID > 0 là khách hàng (hoặc check instance->isCustomer(id) nếu có)
            if (id > 0) {
                customers.push_back({(int)r, (int)n, id});
            }
        }
    }

    if (customers.empty()) {
        return {};
    }

    // 2. Xáo trộn ngẫu nhiên danh sách vị trí
    std::shuffle(customers.begin(), customers.end(), rng);

    // 3. Chọn k khách hàng đầu tiên để xóa
    int actualToRemove = std::min((int)customers.size(), nodesToRemove);

    // Cắt vector để chỉ giữ lại những người cần xóa
    customers.resize(actualToRemove);

    std::vector<int> removedCustomerIds;
    removedCustomerIds.reserve(actualToRemove);

    // Lưu lại ID để trả về cho hàm Repair
    for (const auto& c : customers) {
        removedCustomerIds.push_back(c.customerId);
    }

    // 4. Sắp xếp để xóa an toàn:
    // - Ưu tiên theo Route Index (để gom nhóm xử lý)
    // - Quan trọng: Sắp xếp theo Node Index GIẢM DẦN (Descending)
    //   Lý do: Khi xóa node ở index 5, node ở index 6 sẽ tụt xuống 5.
    //   Nếu xóa index nhỏ trước, index lớn sẽ bị sai lệch.
    std::sort(customers.begin(), customers.end(), [](const CandidateRemoval& a, const CandidateRemoval& b) {
        if (a.routeIndex != b.routeIndex) {
            return a.routeIndex < b.routeIndex;
        }
        return a.nodeIndex > b.nodeIndex; // Index lớn đứng trước
    });

    // 5. Thực hiện xóa trực tiếp dựa trên index đã lưu
    // Vì đã sort giảm dần theo nodeIndex, ta có thể xóa an toàn mà không sợ sai lệch index
    for (const auto& rem : customers) {
        solution.getRoutes()[rem.routeIndex].removeNode(rem.nodeIndex);
    }

    // 6. Cập nhật lại thông số các tuyến
    solution.evaluateRoutes();

    return removedCustomerIds;
}