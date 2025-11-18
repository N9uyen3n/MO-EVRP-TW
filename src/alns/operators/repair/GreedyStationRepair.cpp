// src/alns/operators/repair/GreedyStationRepair.cpp
#include "alns/operators/repair/GreedyStationRepair.h"
#include "core/Vehicle.h"
#include "core/Station.h"
#include "core/Customer.h"
#include "core/Depot.h"
#include <algorithm>
#include <limits>
#include <iostream>
#include <utility>
#include <random>
#include <cmath>

GreedyStationRepair::GreedyStationRepair(std::shared_ptr<Instance> instance)
    : instance(std::move(instance)) {}

std::string GreedyStationRepair::getName() const {
    return "Greedy Station Repair (Enhanced)";
}

// Trọng số đa mục tiêu
const double W_DIST = 0.6;
const double W_CHARGE = 0.3;
const double W_WAIT = 0.1;

/**
 * @brief Tìm vị trí chèn tốt nhất với đánh giá đa mục tiêu
 */
GreedyStationRepair::Insertion
GreedyStationRepair::findBestGreedyStationInsertion(int customerId, Solution& solution) {

    Insertion bestInsertion;
    bestInsertion.cost = std::numeric_limits<double>::infinity();
    bestInsertion.customerId = customerId;

    auto cust = std::dynamic_pointer_cast<Customer>(instance->getNodeById(customerId));
    if (!cust) return bestInsertion;

    for (size_t r_idx = 0; r_idx < solution.getRoutes().size(); ++r_idx) {
        Route& route = solution.getRoutes()[r_idx];

        // Bỏ qua tuyến quá tải
        double currentLoad = 0.0;
        for (int nodeId : route.getNodes()) {
            auto node = instance->getNodeById(nodeId);
            if (auto c = std::dynamic_pointer_cast<Customer>(node)) {
                currentLoad += c->getDemand();
            }
        }

        double vehicleCapacity = instance->getVehicleCapacity();
        double loadRatio = currentLoad / vehicleCapacity;

        // Bỏ qua nếu tuyến gần đầy hoặc không đủ capacity
        if (loadRatio > 0.95 || (currentLoad + cust->getDemand()) > vehicleCapacity) {
            continue;
        }

        // Thử tất cả các vị trí có thể chèn (trừ depot đầu và cuối)
        for (size_t pos = 1; pos < route.getNodes().size(); ++pos) {

            InsertionResult result = route.checkInsertionCost(customerId, pos);

            if (result.isFeasible) {
                // Tính penalty dựa trên độ dài tuyến
                double routeLengthPenalty = 1.0 + (route.getNodes().size() - 2) * 0.03;

                // Chi phí tổng hợp
                double currentCost = (W_DIST * result.deltaDistance +
                                     W_CHARGE * result.deltaChargeAmount +
                                     W_WAIT * result.deltaWaitTime) * routeLengthPenalty;

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

/**
 * @brief Đánh giá chi phí của một trạm sạc trong ngữ cảnh D->S->C hoặc C->S->D
 */
struct StationScore {
    int stationId;
    double costToStation;
    double costFromStation;
    double totalCost;
    double detourRatio; // Tỷ lệ đường vòng so với đường thẳng
};

std::vector<StationScore> evaluateStations(int customerId,
                                           const std::vector<int>& stationIds,
                                           std::shared_ptr<Instance> instance) {
    std::vector<StationScore> scores;

    double directDist = instance->getDistance(0, customerId) + instance->getDistance(customerId, 0);

    for (int stationId : stationIds) {
        StationScore score;
        score.stationId = stationId;

        // Chi phí cho D -> S -> C -> D
        double d_s = instance->getDistance(0, stationId);
        double s_c = instance->getDistance(stationId, customerId);
        double c_d = instance->getDistance(customerId, 0);
        score.costToStation = d_s + s_c + c_d;

        // Chi phí cho D -> C -> S -> D
        double d_c = instance->getDistance(0, customerId);
        double c_s = instance->getDistance(customerId, stationId);
        double s_d = instance->getDistance(stationId, 0);
        score.costFromStation = d_c + c_s + s_d;

        // Chọn chi phí tốt nhất
        score.totalCost = std::min(score.costToStation, score.costFromStation);
        score.detourRatio = score.totalCost / directDist;

        scores.push_back(score);
    }

    // Sắp xếp theo chi phí tốt nhất
    std::sort(scores.begin(), scores.end(),
              [](const StationScore& a, const StationScore& b) {
                  return a.totalCost < b.totalCost;
              });

    return scores;
}

/**
 * @brief Tạo tuyến mới thông minh với nhiều chiến lược
 */
bool createSmartNewRoute(int customerId, Solution& solution,
                        const std::vector<int>& stationIds,
                        std::shared_ptr<Instance> instance) {

    int newRouteId = static_cast<int>(solution.getNumRoutes() + 1);

    auto vehicle = std::make_shared<Vehicle>(
        newRouteId,
        instance->getVehicleCapacity(),
        instance->getVehicleBattery(),
        instance->getVehicleEnergyRate()
    );

    // === CHIẾN LƯỢC 1: Tuyến đơn giản D -> C -> D ===
    Route simpleRoute(newRouteId, vehicle, instance);
    simpleRoute.addNode(customerId, 1);

    if (simpleRoute.isFeasible()) {
        solution.addRoute(simpleRoute);
        return true;
    }

    // === CHIẾN LƯỢC 2: Một trạm sạc (thử các trạm theo thứ tự tối ưu) ===
    std::vector<StationScore> stationScores = evaluateStations(customerId, stationIds, instance);

    for (const auto& score : stationScores) {
        // Thử D -> S -> C -> D
        Route route1(newRouteId, vehicle, instance);
        route1.addNode(score.stationId, 1);
        route1.addNode(customerId, 2);

        if (route1.isFeasible()) {
            solution.addRoute(route1);
            return true;
        }

        // Thử D -> C -> S -> D
        Route route2(newRouteId, vehicle, instance);
        route2.addNode(customerId, 1);
        route2.addNode(score.stationId, 2);

        if (route2.isFeasible()) {
            solution.addRoute(route2);
            return true;
        }
    }

    // === CHIẾN LƯỢC 3: Hai trạm sạc (cho khách hàng rất xa) ===
    // Chỉ thử với 3 trạm tốt nhất để tránh quá nhiều thử nghiệm
    size_t maxStationsToTry = std::min(size_t(3), stationScores.size());

    for (size_t i = 0; i < maxStationsToTry; ++i) {
        for (size_t j = i + 1; j < maxStationsToTry; ++j) {
            int station1 = stationScores[i].stationId;
            int station2 = stationScores[j].stationId;

            // Thử D -> S1 -> C -> S2 -> D
            Route route3(newRouteId, vehicle, instance);
            route3.addNode(station1, 1);
            route3.addNode(customerId, 2);
            route3.addNode(station2, 3);

            if (route3.isFeasible()) {
                solution.addRoute(route3);
                return true;
            }

            // Thử D -> S1 -> S2 -> C -> D
            Route route4(newRouteId, vehicle, instance);
            route4.addNode(station1, 1);
            route4.addNode(station2, 2);
            route4.addNode(customerId, 3);

            if (route4.isFeasible()) {
                solution.addRoute(route4);
                return true;
            }

            // Thử D -> C -> S1 -> S2 -> D
            Route route5(newRouteId, vehicle, instance);
            route5.addNode(customerId, 1);
            route5.addNode(station1, 2);
            route5.addNode(station2, 3);

            if (route5.isFeasible()) {
                solution.addRoute(route5);
                return true;
            }
        }
    }

    return false;
}

void GreedyStationRepair::execute(Solution& solution,
                                  const std::vector<int>& unservedCustomers,
                                  std::mt19937& rng) {

    auto customersToInsert = unservedCustomers;

    // === SẮP XẾP KHÁCH HÀNG THEO ĐỘ KHÓ ===
    // Khách hàng khó (xa + nhu cầu cao) sẽ được xử lý trước
    std::sort(customersToInsert.begin(), customersToInsert.end(),
              [this](int a, int b) {
                  auto custA = std::dynamic_pointer_cast<Customer>(instance->getNodeById(a));
                  auto custB = std::dynamic_pointer_cast<Customer>(instance->getNodeById(b));

                  if (!custA || !custB) return false;

                  // Khoảng cách từ depot
                  double distA = instance->getDistance(0, a) + instance->getDistance(a, 0);
                  double distB = instance->getDistance(0, b) + instance->getDistance(b, 0);

                  // Nhu cầu
                  double demandA = custA->getDemand();
                  double demandB = custB->getDemand();

                  // Độ khó = khoảng cách chuẩn hóa + nhu cầu chuẩn hóa
                  double difficultyA = distA / 100.0 + demandA / instance->getVehicleCapacity();
                  double difficultyB = distB / 100.0 + demandB / instance->getVehicleCapacity();

                  return difficultyA > difficultyB; // Khó nhất trước
              });

    // Lấy danh sách trạm sạc
    std::vector<int> stationIds;
    for (const auto& node : instance->getNodes()) {
        if (std::dynamic_pointer_cast<Station>(node)) {
            int nodeId = node->getId();
            if (nodeId > 1) { // Bỏ qua depot (id=0) và có thể depot clone (id=1)
                stationIds.push_back(nodeId);
            }
        }
    }

    int insertedCount = 0;
    int failedCount = 0;
    int newRoutesCreated = 0;

    // === XỬ LÝ TỪNG KHÁCH HÀNG ===
    for (int customerId : customersToInsert) {

        // Thử chèn vào tuyến hiện có
        Insertion bestInsertion = findBestGreedyStationInsertion(customerId, solution);

        if (bestInsertion.isFeasible) {
            solution.getRoutes()[bestInsertion.routeIndex].addNode(
                customerId, bestInsertion.position
            );
            insertedCount++;
        } else {
            // Tạo tuyến mới với các chiến lược thông minh
            bool success = createSmartNewRoute(customerId, solution, stationIds, instance);

            if (success) {
                insertedCount++;
                newRoutesCreated++;
            } else {
                failedCount++;

                // Thông tin debug chi tiết cho trường hợp thất bại
                auto cust = std::dynamic_pointer_cast<Customer>(instance->getNodeById(customerId));
                double demand = cust ? cust->getDemand() : -1;
                double distToDepot = instance->getDistance(0, customerId);
                double distFromDepot = instance->getDistance(customerId, 0);
                double totalDist = distToDepot + distFromDepot;

                double energyNeeded = totalDist * instance->getVehicleEnergyRate();
                double batteryCapacity = instance->getVehicleBattery();

                std::cerr << "\n=== FAILED TO INSERT CUSTOMER " << customerId << " ===" << std::endl;
                std::cerr << "  Demand: " << demand
                          << " / Vehicle Capacity: " << instance->getVehicleCapacity() << std::endl;
                std::cerr << "  Distance: D->" << distToDepot << "->C->" << distFromDepot << "->D"
                          << " (Total: " << totalDist << ")" << std::endl;
                std::cerr << "  Energy: Needed=" << energyNeeded
                          << " / Battery=" << batteryCapacity
                          << " (Ratio: " << (energyNeeded/batteryCapacity) << ")" << std::endl;
                std::cerr << "  Stations tried: " << stationIds.size()
                          << " (configs: simple + 2*N single-station + 5*N(N-1)/2 two-station)" << std::endl;
            }
        }
    }

    // Đánh giá lại toàn bộ solution
    solution.evaluateRoutes();

    // Log kết quả
    // std::cout << "\n[GreedyStationRepair] Results:" << std::endl;
    // std::cout << "  Inserted into existing routes: " << (insertedCount - newRoutesCreated) << std::endl;
    // std::cout << "  New routes created: " << newRoutesCreated << std::endl;
    // std::cout << "  Failed: " << failedCount << std::endl;
    // std::cout << "  Total processed: " << customersToInsert.size() << std::endl;
    // std::cout << "  Success rate: " << (100.0 * insertedCount / customersToInsert.size()) << "%" << std::endl;
}