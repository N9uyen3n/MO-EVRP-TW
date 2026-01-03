#include "../../../../include/alns/operators/repair/GreedyEnergyInsertion.h"
#include "../../../../include/core/Solution.h"
#include "../../../../include/core/Instance.h"
#include "../../../../include/core/Vehicle.h" // [QUAN TRỌNG] Cần để tạo xe mới
#include <algorithm>
#include <limits>
#include <vector>

GreedyEnergyInsertion::GreedyEnergyInsertion(std::shared_ptr<Instance> inst)
    : instance(inst) {}

// getName
std::string GreedyEnergyInsertion::getName() const {
    return "Greedy Energy Insertion";
}

void GreedyEnergyInsertion::execute(Solution& solution, const std::vector<int>& unservedCustomers, std::mt19937& rng) {
    // Copy và xáo trộn danh sách khách hàng
    std::vector<int> customers = unservedCustomers;
    std::shuffle(customers.begin(), customers.end(), rng);

    auto& routes = solution.getRoutes();

    for (int customerId : customers) {
        int bestRouteIdx = -1;
        int bestPos = -1;
        double minCost = std::numeric_limits<double>::max();

        // --- BƯỚC 1: Tìm vị trí chèn trong các tuyến CŨ ---
        for (int r = 0; r < routes.size(); ++r) {
            const auto& nodes = routes[r].getNodes();
            // Duyệt các vị trí chèn (vị trí j+1)
            for (size_t j = 0; j < nodes.size() - 1; ++j) {

                InsertionResult res = routes[r].checkInsertionCost(customerId, j + 1);

                if (res.isFeasible) {
                    // MỤC TIÊU: Minimize Charge Amount
                    double energyCost = res.deltaChargeAmount;

                    // Tie-breaker: Nếu không tốn thêm năng lượng sạc (energyCost == 0),
                    // ưu tiên vị trí đi quãng đường ngắn hơn để tiết kiệm pin tiêu thụ.
                    if (energyCost < 1e-6) {
                        energyCost = res.deltaDistance * 0.0001;
                    }

                    if (energyCost < minCost) {
                        minCost = energyCost;
                        bestRouteIdx = r;
                        bestPos = j + 1;
                    }
                }
            }
        }

        // --- BƯỚC 2: Thực hiện chèn hoặc TẠO TUYẾN MỚI ---
        if (bestRouteIdx != -1) {
            // Chèn vào tuyến cũ
            routes[bestRouteIdx].addNode(customerId, bestPos);
            routes[bestRouteIdx].evaluate();
        } else {
            // [BỔ SUNG QUAN TRỌNG] Nếu không chèn được vào đâu -> Tạo tuyến mới
            int newRouteId = solution.getNumRoutes();

            // Tạo xe mới từ thông số của Instance
            auto vehicle = std::make_shared<Vehicle>(
                newRouteId,
                instance->getVehicleCapacity(),
                instance->getVehicleBattery(),
                instance->getVehicleEnergyRate()
            );

            // Tạo tuyến mới
            Route newRoute(newRouteId, vehicle, instance);

            // Thêm khách hàng vào giữa 2 depot (vị trí 1)
            newRoute.addNode(customerId, 1);
            newRoute.evaluate();

            // Cập nhật solution
            solution.addRoute(newRoute);
        }
    }
}