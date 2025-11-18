// src/alns/operators/repair/GreedyRepair.cpp
#include "alns/operators/repair/GreedyRepair.h"
#include "core/Vehicle.h"
#include "core/Station.h"   // <-- THÊM VÀO
#include "core/Customer.h"  // <-- THÊM VÀO
#include "core/Depot.h"     // <-- THÊM VÀO
#include <algorithm>
#include <limits>
#include <iostream>
#include <utility>         // Cho std::move

GreedyRepair::GreedyRepair(std::shared_ptr<Instance> instance)
    : instance(std::move(instance)) {}

std::string GreedyRepair::getName() const {
    return "Greedy Repair";
}

static constexpr double W_DIST = 0.7;
static constexpr double W_CHARGE = 0.3;

/**
 * @brief (Hàm nội bộ) Tìm vị trí chèn TỐT NHẤT (dùng delta-cost)
 */
GreedyRepair::Insertion
GreedyRepair::findBestInsertion(int customerId, Solution& solution) {

    Insertion bestInsertion;
    bestInsertion.cost = std::numeric_limits<double>::infinity();
    bestInsertion.customerId = customerId;

    for (size_t r_idx = 0; r_idx < solution.getRoutes().size(); ++r_idx) {
        const Route& route = solution.getRoutes()[r_idx];

        for (size_t pos = 1; pos < route.getNodes().size(); ++pos) {
            InsertionResult result = route.checkInsertionCost(customerId, pos);

            if (result.isFeasible) {
                double currentCost = W_DIST * result.deltaDistance +
                                     W_CHARGE * result.deltaChargeAmount;

                if (currentCost < bestInsertion.cost) {
                    bestInsertion.cost = currentCost;
                    bestInsertion.routeIndex = r_idx;
                    bestInsertion.position = pos;
                    bestInsertion.isFeasible = true;
                }
            }
        }
    }
    return bestInsertion;
}


void GreedyRepair::execute(Solution& solution, const std::vector<int>& unservedCustomers, std::mt19937& rng) {

    auto customersToInsert = unservedCustomers;
    std::shuffle(customersToInsert.begin(), customersToInsert.end(), rng);

    // Lấy danh sách ID các trạm sạc (chỉ lấy 1 lần)
    std::vector<int> stationIds;
    for (const auto& node : instance->getNodes()) {
        // Chỉ lấy trạm sạc (ID > 0), không lấy S0 (ID=1) ở depot
        if (std::dynamic_pointer_cast<Station>(node) && node->getId() > 1) {
            stationIds.push_back(node->getId());
        }
    }


    for (int customerId : customersToInsert) {

        Insertion bestInsertion = findBestInsertion(customerId, solution);

        if (bestInsertion.isFeasible) {
            // TRƯỜNG HỢP 1: Chèn vào tuyến hiện có (OK)
            solution.getRoutes()[bestInsertion.routeIndex].addNode(
                customerId, bestInsertion.position
            );
        } else {
            // TRƯỜNG HỢP 2: Không chèn được, phải TẠO TUYẾN MỚI

            bool addedSuccessfully = false;

            // Tạo xe và tuyến mới (chỉ 1 lần)
            auto vehicle = std::make_shared<Vehicle>(
                static_cast<int>(solution.getNumRoutes() + 1),
                instance->getVehicleCapacity(),
                instance->getVehicleBattery(), // Pin gốc (ví dụ: 77.75)
                instance->getVehicleEnergyRate()
            );
            Route newRoute(static_cast<int>(solution.getNumRoutes() + 1), vehicle, instance);

            // --- BƯỚC A: Thử tuyến đơn giản D -> C -> D ---
            newRoute.addNode(customerId, 1); // Thử D -> C -> D
            newRoute.evaluate();

            if (newRoute.isFeasible()) {
                solution.addRoute(newRoute);
                addedSuccessfully = true;
            } else {
                // --- BƯỚC B: Thất bại, thử chèn trạm sạc ---
                // (GHI CHÚ: Logic này có thể được tối ưu hóa bằng cách
                //  chỉ thử các trạm sạc "trên đường đi", nhưng hiện tại ta thử tất cả)

                for (int stationId : stationIds) {
                    // Thử kịch bản D -> S -> C -> D
                    newRoute.clear(); // Reset về D -> D
                    newRoute.addNode(stationId, 1);  // Thêm S tại vị trí 1
                    newRoute.addNode(customerId, 2); // Thêm C tại vị trí 2

                    if (newRoute.isFeasible()) {
                        solution.addRoute(newRoute);
                        addedSuccessfully = true;
                        break; // Thoát vòng lặp trạm sạc
                    }

                    // Thử kịch bản D -> C -> S -> D
                    newRoute.clear(); // Reset về D -> D
                    newRoute.addNode(customerId, 1); // Thêm C tại vị trí 1
                    newRoute.addNode(stationId, 2); // Thêm S tại vị trí 2

                    if (newRoute.isFeasible()) {
                        solution.addRoute(newRoute);
                        addedSuccessfully = true;
                        break; // Thoát vòng lặp trạm sạc
                    }
                }
            }

            // --- BƯỚC C: Nếu tất cả đều thất bại, báo lỗi ---
            if (!addedSuccessfully) {
                // (In lại code debug cũ của bạn, giờ nó chính xác hơn)
                auto cust = std::dynamic_pointer_cast<Customer>(instance->getNodeById(customerId));
                double energy_needed_simple = (instance->getDistance(0, customerId) + instance->getDistance(customerId, 0)) * vehicle->getEnergyConsumptionRate();

                std::cerr << "--- DEBUG INFO (FINAL FAIL) CHO CUSTOMER " << customerId << " ---" << std::endl;
                std::cerr << "  Cust Demand: " << cust->getDemand() << " | Veh Capacity: " << vehicle->getCapacity() << std::endl;
                std::cerr << "  Energy Needed (D->C->D): " << energy_needed_simple << " | Veh Battery: " << vehicle->getBatteryCapacity() << std::endl;
                std::cerr << "  (Cũng đã thử " << stationIds.size() << " trạm sạc (x2 kịch bản) nhưng thất bại)" << std::endl;
                std::cerr << "WARNING: Customers " << customerId << " Cannot insert (GreedyRepair)!" << std::endl;
            }
        }
    }

    solution.evaluateRoutes();
}