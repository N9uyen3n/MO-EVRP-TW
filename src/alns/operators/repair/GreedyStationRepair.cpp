#include "../../../../include/alns/operators/repair/GreedyStationRepair.h"
#include "../../../../include/core/Solution.h"
#include "../../../../include/core/Vehicle.h"
#include "../../../../include/core/Route.h" // THÊM INCLUDE QUAN TRỌNG
#include <algorithm>
#include <vector>
#include <limits>

GreedyStationRepair::GreedyStationRepair(std::shared_ptr<Instance> instance)
    : instance(instance) {}

std::string GreedyStationRepair::getName() const {
    return "Greedy Station Repair";
}

void GreedyStationRepair::execute(Solution& solution, const std::vector<int>& unservedCustomers, std::mt19937& rng) {
    // Copy và xáo trộn danh sách khách hàng
    std::vector<int> customers = unservedCustomers;
    std::shuffle(customers.begin(), customers.end(), rng);

    auto& routes = solution.getRoutes();

    for (int customerId : customers) {
        Insertion bestInsertion = findBestGreedyStationInsertion(customerId, solution);

        // --- TRƯỜNG HỢP 1: Chèn vào tuyến hiện có ---
        if (bestInsertion.routeIndex != -1) {
            auto& route = routes[bestInsertion.routeIndex];

            if (bestInsertion.requiresStation) {
                // Logic chèn: Muốn kết quả là ... -> A -> Station -> Customer -> B -> ...
                // Bước 1: Chèn Customer vào vị trí.
                route.addNode(bestInsertion.customerId, bestInsertion.position);
                // Bước 2: Chèn Station vào CÙNG vị trí đó (đẩy Customer ra sau).
                route.addNode(bestInsertion.stationId, bestInsertion.position);
            } else {
                // Chèn bình thường (chỉ Customer)
                route.addNode(customerId, bestInsertion.position);
            }
            route.evaluate();
        }
        // --- TRƯỜNG HỢP 2: Tạo tuyến mới (Nếu không thể chèn vào đâu) ---
        else {
            int newRouteId = solution.getNumRoutes();

            auto vehicle = std::make_shared<Vehicle>(
                newRouteId,
                instance->getVehicleCapacity(),
                instance->getVehicleBattery(),
                instance->getVehicleEnergyRate()
            );

            Route newRoute(newRouteId, vehicle, instance);
            newRoute.addNode(customerId, 1);
            newRoute.evaluate();
            solution.addRoute(newRoute);
        }
    }
}

GreedyStationRepair::Insertion GreedyStationRepair::findBestGreedyStationInsertion(int customerId, Solution& solution) {
    Insertion bestIns; // Mặc định cost là infinity
    auto& routes = solution.getRoutes();

    int nearestStationId = instance->getNearestStationId(customerId);

    for (int r = 0; r < routes.size(); ++r) {
        // Duyệt các vị trí chèn (vị trí j+1)
        for (size_t j = 0; j < routes[r].getNodes().size() - 1; ++j) {
            size_t position = j + 1;

            // 1. Thử chèn bình thường (Chỉ khách hàng)
            InsertionResult res = {false}; // Mặc định là infeasible
            
            // --- TỐI ƯU HÓA: Chỉ gọi checkInsertionCost nếu qua được bounding check ---
            if (routes[r].canPossiblyInsert(customerId, position)) {
                res = routes[r].checkInsertionCost(customerId, position);
            }

            if (res.isFeasible) {
                if (res.deltaDistance < bestIns.cost) {
                    bestIns = {customerId, r, position, res.deltaDistance, true, false, -1, 0};
                }
            } else {
                // 2. Nếu INFEASIBLE, thử chèn kèm Trạm sạc gần nhất
                if (nearestStationId != -1) {
                    // [AGGRESSIVE FIX] Prevent inserting a station if the previous node is already a station.
                    auto prevNode = instance->getNodeById(routes[r].getNodeAt(position - 1));
                    if (prevNode->getType() == NodeType::STATION) {
                        continue; // Skip this insertion if the previous node is any station.
                    }

                    // TẠO BẢN SAO ĐỂ KIỂM TRA AN TOÀN
                    Route routeCopy = routes[r];

                    // Chèn thử Customer và Station vào bản sao
                    routeCopy.addNode(customerId, position);
                    routeCopy.addNode(nearestStationId, position);

                    // GỌI EVALUATE() TRÊN BẢN SAO
                    routeCopy.evaluate();

                    // KIỂM TRA KẾT QUẢ THÔNG QUA GETTERS CỦA ROUTE
                    if (routeCopy.isFeasible()) {
                        // Tính chi phí tăng thêm (delta distance) so với tuyến gốc
                        double deltaCost = routeCopy.getTotalDistance() - routes[r].getTotalDistance();

                        // So sánh với chi phí tốt nhất hiện tại
                        if (deltaCost < bestIns.cost) {
                            bestIns = {
                                customerId,
                                r,
                                position,
                                deltaCost,
                                true,   // isFeasible
                                true,   // requiresStation
                                nearestStationId,
                                position // stationPosition
                            };
                        }
                    }
                }
            }
        }
    }
    return bestIns;
}
