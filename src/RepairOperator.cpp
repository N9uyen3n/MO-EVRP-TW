// SỬA: Thêm include này ở dòng ĐẦU TIÊN
#include "Operators.h"

#include "Customer.h"
#include "Station.h"
#include <iostream>
#include <algorithm> // cho std::shuffle, std::sort
#include <vector>

// --- Định nghĩa hàm so sánh cho InsertionResult ---
bool InsertionResult::operator<(const InsertionResult& other) const {
    return cost < other.cost;
}

// ==================================================================
// SỬA CHỮA KHÁCH HÀNG (CUSTOMER REPAIR)
// ==================================================================

ICustomerRepair::ICustomerRepair(std::shared_ptr<Instance> instance, std::mt19937& rng)
    : instance(instance), rng(rng) {}

// --- Các hàm Helper của ICustomerRepair ---
InsertionResult ICustomerRepair::findBestInsertionForCustomer(int customerId, const Solution& solution) {
    InsertionResult best;
    best.customerId = customerId;
    for (size_t i = 0; i < solution.getRoutes().size(); ++i) {
        const Route& route = solution.getRoutes()[i];
        for (size_t pos = 1; pos < route.getNodes().size(); ++pos) {
            Route tempRoute = route;
            tempRoute.addNode(customerId, pos);
            if (tempRoute.isFeasible()) {
                double insertionCost = tempRoute.getTotalDistance() - route.getTotalDistance();
                if (insertionCost < best.cost) {
                    best.cost = insertionCost;
                    best.routeIndex = i;
                    best.position = pos;
                    best.feasible = true;
                }
            }
        }
    }
    return best;
}

std::vector<InsertionResult> ICustomerRepair::findKBestInsertionsForCustomer(int customerId, const Solution& solution, int k) {
    std::vector<InsertionResult> allInsertions;
    for (size_t i = 0; i < solution.getRoutes().size(); ++i) {
        const Route& route = solution.getRoutes()[i];
        for (size_t pos = 1; pos < route.getNodes().size(); ++pos) {
            Route tempRoute = route;
            tempRoute.addNode(customerId, pos);
            if (tempRoute.isFeasible()) {
                double insertionCost = tempRoute.getTotalDistance() - route.getTotalDistance();
                allInsertions.push_back({customerId, static_cast<int>(i), pos, insertionCost, true});
            }
        }
    }

    std::sort(allInsertions.begin(), allInsertions.end());
    if (allInsertions.size() > k) {
        allInsertions.resize(k);
    }
    return allInsertions;
}

void ICustomerRepair::createNewRouteForCustomer(Solution& solution, int custId) {
    std::shared_ptr<Vehicle> newVehicle = std::make_shared<Vehicle>(
        solution.getNumRoutes(),
        instance->getVehicleCapacity(),
        instance->getVehicleBattery(),
        instance->getVehicleEnergyRate()
    );
    Route newRoute(solution.getNumRoutes(), newVehicle, instance);
    newRoute.addNode(custId, 1);

    if (newRoute.isFeasible()) {
        solution.addRoute(newRoute);
    } else {
         std::cerr << "RepairOp: Khong the phuc vu khach hang " << custId << " (tham chi o tuyen moi)" << std::endl;
    }
}

// --- GreedyInsertion ---
void GreedyInsertion::repair(Solution& solution, std::vector<int>& unservedCustomers) {
    std::shuffle(unservedCustomers.begin(), unservedCustomers.end(), rng);

    while (!unservedCustomers.empty()) {
        InsertionResult bestOverallInsertion;
        int bestCustomerIndex = -1;

        for (size_t i = 0; i < unservedCustomers.size(); ++i) {
            int custId = unservedCustomers[i];
            InsertionResult bestForThisCust = findBestInsertionForCustomer(custId, solution);

            if (bestForThisCust.feasible && bestForThisCust.cost < bestOverallInsertion.cost) {
                bestOverallInsertion = bestForThisCust;
                bestCustomerIndex = i;
            }
        }

        if (bestCustomerIndex != -1) {
            solution.getRoutes()[bestOverallInsertion.routeIndex].addNode(
                bestOverallInsertion.customerId,
                bestOverallInsertion.position
            );
            unservedCustomers.erase(unservedCustomers.begin() + bestCustomerIndex);
        } else {
            int custId = unservedCustomers.front();
            unservedCustomers.erase(unservedCustomers.begin());
            createNewRouteForCustomer(solution, custId);
        }
    }
}

// --- RegretKInsertion ---
RegretKInsertion::RegretKInsertion(std::shared_ptr<Instance> inst, std::mt19937& r, int k_val)
    : ICustomerRepair(inst, r), k(k_val) {}

void RegretKInsertion::repair(Solution& solution, std::vector<int>& unservedCustomers) {
    while (!unservedCustomers.empty()) {
        double maxRegret = -1.0;
        int bestCustomerIndex = -1;
        InsertionResult bestInsertionForRegretfulCustomer;

        for (size_t i = 0; i < unservedCustomers.size(); ++i) {
            int custId = unservedCustomers[i];

            std::vector<InsertionResult> kBest = findKBestInsertionsForCustomer(custId, solution, k);
            if (kBest.empty()) continue;

            double regret = 0.0;
            double bestCost = kBest[0].cost;
            for (size_t j = 1; j < kBest.size(); ++j) {
                regret += (kBest[j].cost - bestCost);
            }

            if (regret > maxRegret) {
                maxRegret = regret;
                bestCustomerIndex = i;
                bestInsertionForRegretfulCustomer = kBest[0];
            }
        }

        if (bestCustomerIndex != -1) {
            solution.getRoutes()[bestInsertionForRegretfulCustomer.routeIndex].addNode(
                bestInsertionForRegretfulCustomer.customerId,
                bestInsertionForRegretfulCustomer.position
            );
            unservedCustomers.erase(unservedCustomers.begin() + bestCustomerIndex);
        } else {
            int custId = unservedCustomers.front();
            unservedCustomers.erase(unservedCustomers.begin());
            createNewRouteForCustomer(solution, custId);
        }
    }
}

// ==================================================================
// SỬA CHỮA TRẠM SẠC (STATION REPAIR)
// ==================================================================

IStationRepair::IStationRepair(std::shared_ptr<Instance> instance, std::mt19937& rng)
    : instance(instance), rng(rng) {}

// --- GreedyStationInsertion ---
void GreedyStationInsertion::repair(Solution& solution) {
    std::vector<int> allStationIds;
    for(const auto& s_id_tuple : instance->getStationIds()) {
        allStationIds.push_back(std::get<0>(s_id_tuple));
    }
    if (allStationIds.empty()) return;

    for (auto& route : solution.getRoutes()) {
        route.evaluate();

        bool repaired = false; // Cờ để tránh vòng lặp vô hạn
        int attemptLimit = route.getNodes().size() + allStationIds.size(); // Giới hạn số lần thử
        int attempts = 0;

        while (!route.isFeasible() && !repaired && attempts < attemptLimit) {
            attempts++;
            // *** Tìm vị trí lỗi (logic đơn giản hóa) ***
            // logic `evaluate` của bạn không cho biết *nơi* nó hỏng.
            // Chúng ta cần 1 hàm `findFirstInfeasibility`
            // Tạm thời, ta chỉ thử chèn vào trước *mọi* khách hàng

            int firstBadNodeIdx = -1;
            // (Cần logic mô phỏng lại evaluate để tìm lỗi pin)
            // ...
            // Giả sử tìm được lỗi ở `firstBadNodeIdx = k`

            // --- Logic tạm thời: Thử chèn vào mọi vị trí ---
            // (Logic này không hiệu quả, nhưng sẽ compile)
            int bestInsertPos = -1;
            int bestStationId = -1;
            double minResultingDistance = std::numeric_limits<double>::infinity();

            for (size_t pos = 1; pos < route.getNodes().size(); ++pos) {
                for (int stationId : allStationIds) {
                    Route tempRoute = route;
                    tempRoute.addNode(stationId, pos);
                    if (tempRoute.isFeasible()) {
                        if (tempRoute.getTotalDistance() < minResultingDistance) {
                            minResultingDistance = tempRoute.getTotalDistance();
                            bestInsertPos = pos;
                            bestStationId = stationId;
                        }
                    }
                }
            }

            if (bestInsertPos != -1) {
                // Sửa thành công
                route.addNode(bestStationId, bestInsertPos);
                repaired = true; // Đã sửa, thoát vòng lặp
            } else {
                // Không thể sửa
                break; // Thoát vòng lặp while
            }
        }
    }
}