// src/alns/operators/repair/RepairUtils.cpp
#include "alns/operators/repair/RepairUtils.h"
#include "core/Vehicle.h"
#include "core/Station.h"
#include "core/Customer.h"
#include "core/Route.h"
#include <algorithm>
#include <iostream>
#include <cmath>

namespace RepairUtils {

std::vector<StationScore> evaluateStations(int customerId,
                                           const std::vector<int>& stationIds,
                                           const std::shared_ptr<Instance>& instance) {
    std::vector<StationScore> scores;
    if (stationIds.empty()) {
        return scores;
    }

    double directDist = instance->getDistance(0, customerId) + instance->getDistance(customerId, 0);
    if (directDist == 0) directDist = 1; // Avoid division by zero

    for (int stationId : stationIds) {
        StationScore score;
        score.stationId = stationId;

        // Cost for D -> S -> C -> D
        double d_s = instance->getDistance(0, stationId);
        double s_c = instance->getDistance(stationId, customerId);
        double c_d = instance->getDistance(customerId, 0);
        score.costToStation = d_s + s_c + c_d;

        // Cost for D -> C -> S -> D
        double d_c = instance->getDistance(0, customerId);
        double c_s = instance->getDistance(customerId, stationId);
        double s_d = instance->getDistance(stationId, 0);
        score.costFromStation = d_c + c_s + s_d;

        // Choose the best cost
        score.totalCost = std::min(score.costToStation, score.costFromStation);
        score.detourRatio = score.totalCost / directDist;

        scores.push_back(score);
    }

    // Sort by the best total cost
    std::sort(scores.begin(), scores.end(),
              [](const StationScore& a, const StationScore& b) {
                  return a.totalCost < b.totalCost;
              });

    return scores;
}

bool createSmartNewRoute(int customerId,
                         Solution& solution,
                         const std::vector<int>& stationIds,
                         const std::shared_ptr<Instance>& instance) {

    int newRouteId = static_cast<int>(solution.getNumRoutes() + 1);

    auto vehicle = std::make_shared<Vehicle>(
        newRouteId,
        instance->getVehicleCapacity(),
        instance->getVehicleBattery(),
        instance->getVehicleEnergyRate()
    );

    // === STRATEGY 1: Simple Route D -> C -> D ===
    Route simpleRoute(newRouteId, vehicle, instance);
    simpleRoute.addNode(customerId, 1);

    if (simpleRoute.isFeasible()) {
        solution.addRoute(simpleRoute);
        return true;
    }

    // === STRATEGY 2: One Charging Station (try stations in optimal order) ===
    std::vector<StationScore> stationScores = evaluateStations(customerId, stationIds, instance);

    for (const auto& score : stationScores) {
        // Try D -> S -> C -> D
        Route route1(newRouteId, vehicle, instance);
        route1.addNode(score.stationId, 1);
        route1.addNode(customerId, 2);

        if (route1.isFeasible()) {
            solution.addRoute(route1);
            return true;
        }

        // Try D -> C -> S -> D
        Route route2(newRouteId, vehicle, instance);
        route2.addNode(customerId, 1);
        route2.addNode(score.stationId, 2);

        if (route2.isFeasible()) {
            solution.addRoute(route2);
            return true;
        }
    }

    // === STRATEGY 3: Two Charging Stations (for very remote customers) ===
    // Only try with the best 3 stations to avoid excessive computation
    size_t maxStationsToTry = std::min(size_t(3), stationScores.size());

    for (size_t i = 0; i < maxStationsToTry; ++i) {
        for (size_t j = i + 1; j < maxStationsToTry; ++j) {
            int station1 = stationScores[i].stationId;
            int station2 = stationScores[j].stationId;

            // Try D -> S1 -> C -> S2 -> D
            Route route3(newRouteId, vehicle, instance);
            route3.addNode(station1, 1);
            route3.addNode(customerId, 2);
            route3.addNode(station2, 3);

            if (route3.isFeasible()) {
                solution.addRoute(route3);
                return true;
            }

            // Try D -> S1 -> S2 -> C -> D
            Route route4(newRouteId, vehicle, instance);
            route4.addNode(station1, 1);
            route4.addNode(station2, 2);
            route4.addNode(customerId, 3);

            if (route4.isFeasible()) {
                solution.addRoute(route4);
                return true;
            }

            // Try D -> C -> S1 -> S2 -> D
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

    return false; // Failed to create any feasible new route
}

} // namespace RepairUtils
