#include "../../../../include/alns/operators/repair/GreedyTimeInsertion.h"
#include "../../../../include/core/Solution.h"
#include "../../../../include/core/Vehicle.h" // THÊM INCLUDE
#include <algorithm>
#include <limits>

GreedyTimeInsertion::GreedyTimeInsertion(std::shared_ptr<Instance> inst)
    : instance(inst) {}

// 2. Triển khai getName
std::string GreedyTimeInsertion::getName() const {
    return "Greedy Time Insertion";
}

void GreedyTimeInsertion::execute(Solution& solution, const std::vector<int>& unservedCustomers, std::mt19937& rng) {
    std::vector<int> customers = unservedCustomers;
    std::shuffle(customers.begin(), customers.end(), rng);
    auto& routes = solution.getRoutes();

    for (int customerId : customers) {
        int bestRouteIdx = -1;
        int bestPos = -1;
        double minCost = std::numeric_limits<double>::max();

        for (int r = 0; r < routes.size(); ++r) {
            const auto& nodes = routes[r].getNodes();
            for (size_t j = 0; j < nodes.size() - 1; ++j) {
                
                InsertionResult res = routes[r].checkInsertionCost(customerId, j + 1);

                if (res.isFeasible) {
                    // MỤC TIÊU: Minimize Time (Wait + Travel)
                    // Travel time thường tỉ lệ thuận với Distance.
                    // Cost = Delta Wait Time + Delta Distance (proxy cho travel time)
                    double timeCost = res.deltaWaitTime + res.deltaDistance;

                    if (timeCost < minCost) {
                        minCost = timeCost;
                        bestRouteIdx = r;
                        bestPos = j + 1;
                    }
                }
            }
        }

        if (bestRouteIdx != -1) {
            routes[bestRouteIdx].addNode(customerId, bestPos);
            routes[bestRouteIdx].evaluate();
        } else {
            // [SỬA] NẾU KHÔNG CHÈN ĐƯỢC -> TẠO TUYẾN MỚI
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
