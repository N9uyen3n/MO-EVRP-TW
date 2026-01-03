#include "../../../../include/alns/operators/repair/GreedyDistanceInsertion.h"
#include "../../../../include/core/Solution.h"
#include "../../../../include/core/Route.h"
#include "../../../../include/core/Vehicle.h" // [BẮT BUỘC] Để tạo xe mới
#include <algorithm>
#include <limits>
#include <vector>

// 1. Triển khai Constructor
GreedyDistanceInsertion::GreedyDistanceInsertion(std::shared_ptr<Instance> inst)
    : instance(inst) {}

// 2. Triển khai getName
std::string GreedyDistanceInsertion::getName() const {
    return "Greedy Distance Insertion";
}

// 3. Triển khai execute
void GreedyDistanceInsertion::execute(Solution& solution, const std::vector<int>& unservedCustomers, std::mt19937& rng) {
    // Copy danh sách để xử lý và xáo trộn ngẫu nhiên
    std::vector<int> customers = unservedCustomers;
    std::shuffle(customers.begin(), customers.end(), rng);

    auto& routes = solution.getRoutes();

    for (int customerId : customers) {
        int bestRouteIdx = -1;
        int bestPos = -1;
        double minCost = std::numeric_limits<double>::max();

        // --- BƯỚC 1: Tìm vị trí chèn tốt nhất trong các tuyến hiện có ---
        for (int r = 0; r < routes.size(); ++r) {
            const auto& nodes = routes[r].getNodes();
            // Duyệt các vị trí (chèn vào sau node j, tức là vị trí j+1)
            for (size_t j = 0; j < nodes.size() - 1; ++j) {

                InsertionResult res = routes[r].checkInsertionCost(customerId, j + 1);

                if (res.isFeasible) {
                    // MỤC TIÊU: Minimize Delta Distance (Quãng đường tăng thêm ít nhất)
                    if (res.deltaDistance < minCost) {
                        minCost = res.deltaDistance;
                        bestRouteIdx = r;
                        bestPos = j + 1;
                    }
                }
            }
        }

        // --- BƯỚC 2: Thực hiện chèn hoặc TẠO TUYẾN MỚI ---
        if (bestRouteIdx != -1) {
            // Trường hợp A: Chèn vào tuyến cũ
            routes[bestRouteIdx].addNode(customerId, bestPos);
            routes[bestRouteIdx].evaluate(); // Cập nhật trạng thái tuyến
        } else {
            // Trường hợp B: Không tìm thấy vị trí khả thi -> TẠO TUYẾN MỚI
            // Nếu không làm bước này, khách hàng sẽ bị bỏ sót -> Solution Invalid.

            int newRouteId = solution.getNumRoutes();

            // Tạo xe mới dựa trên thông số từ Instance
            auto vehicle = std::make_shared<Vehicle>(
                newRouteId,
                instance->getVehicleCapacity(),     // 1. Capacity
                instance->getVehicleBattery(),      // 2. Battery
                instance->getVehicleEnergyRate()    // 3. EnergyRate
            );

            // Khởi tạo tuyến mới
            Route newRoute(newRouteId, vehicle, instance);

            // Chèn khách hàng vào vị trí 1 (giữa 2 depot: 0 -> Khách -> 0)
            newRoute.addNode(customerId, 1);
            newRoute.evaluate();

            // Thêm tuyến vào giải pháp
            solution.addRoute(newRoute);
        }
    }
}