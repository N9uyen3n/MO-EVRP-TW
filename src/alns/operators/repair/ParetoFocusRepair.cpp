#include "../../../../include/alns/operators/repair/ParetoFocusRepair.h"
#include "../../../../include/core/Solution.h"
#include "../../../../include/core/Instance.h"
#include "../../../../include/core/Vehicle.h"
#include <algorithm>
#include <limits>
#include <iostream>
#include <numeric>

ParetoFocusRepair::ParetoFocusRepair(std::shared_ptr<Instance> inst)
    : instance(inst) {}

std::string ParetoFocusRepair::getName() const {
    return "Pareto Focus Repair";
}

void ParetoFocusRepair::execute(Solution& solution, const std::vector<int>& unservedCustomers, std::mt19937& rng) {
    // 1. CHỌN MỤC TIÊU NGẪU NHIÊN (0: Dist, 1: Time, 2: Workload)
    std::uniform_int_distribution<> distObj(0, 2);
    int focusObj = distObj(rng);

    std::vector<int> customers = unservedCustomers;
    std::shuffle(customers.begin(), customers.end(), rng);

    auto& routes = solution.getRoutes();

    // Tính toán trước các giá trị cần thiết cho mục tiêu Workload
    double meanRouteDuration = 0.0;
    if (focusObj == 2 && !routes.empty()) {
        double totalDuration = 0;
        for(const auto& route : routes) {
            totalDuration += route.getTotalTime();
        }
        meanRouteDuration = totalDuration / routes.size();
    }

    for (int customerId : customers) {
        int bestRouteIdx = -1;
        int bestPos = -1;
        double minDeltaCost = std::numeric_limits<double>::max();
        auto customerNode = instance->getNodeById(customerId);

        for (int r = 0; r < routes.size(); ++r) {
            for (size_t j = 0; j < routes[r].getNodes().size() - 1; ++j) {
                InsertionResult res = routes[r].checkInsertionCost(customerId, j + 1);

                if (!res.isFeasible) continue;

                double currentCost = 0.0;

                if (focusObj == 0) { // Distance
                    currentCost = res.deltaDistance;
                }
                else if (focusObj == 1) { // Time
                    // Ước lượng chi phí thời gian tăng thêm
                    currentCost = res.deltaWaitTime + res.deltaDistance; // Dùng distance làm proxy cho travel time
                }
                else { // Workload
                    // Heuristic: Tìm vị trí chèn sao cho thời gian mới của tuyến gần với trung bình nhất
                    // Ước lượng thời gian mới của tuyến
                    double estimatedDeltaTime = res.deltaWaitTime + res.deltaDistance + customerNode->getServiceTime();
                    double newRouteTime = routes[r].getTotalTime() + estimatedDeltaTime;
                    currentCost = std::abs(newRouteTime - meanRouteDuration);
                }

                if (currentCost < minDeltaCost) {
                    minDeltaCost = currentCost;
                    bestRouteIdx = r;
                    bestPos = j + 1;
                }
            }
        }

        if (bestRouteIdx != -1) {
            routes[bestRouteIdx].addNode(customerId, bestPos);
            routes[bestRouteIdx].evaluate();
        } else {
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
