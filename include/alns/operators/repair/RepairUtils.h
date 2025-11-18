#pragma once

#include "core/Solution.h"
#include "core/Instance.h"
#include <vector>
#include <memory>
#include <random>

// Forward declarations to avoid circular dependencies
class Solution;
class Instance;

namespace RepairUtils {

    /**
     * @brief Holds evaluation scores for a potential station insertion.
     */
    struct StationScore {
        int stationId;
        double costToStation;
        double costFromStation;
        double totalCost;
        double detourRatio; // Ratio of detour distance vs. direct distance
    };

    /**
     * @brief Evaluates and ranks charging stations based on the minimum detour
     * required to visit a customer.
     * @param customerId The ID of the customer to insert.
     * @param stationIds A list of available station IDs.
     * @param instance The problem instance.
     * @return A vector of StationScore, sorted by the best total cost.
     */
    std::vector<StationScore> evaluateStations(int customerId,
                                               const std::vector<int>& stationIds,
                                               const std::shared_ptr<Instance>& instance);

    /**
     * @brief Intelligently creates a new route for a single customer.
     * It tries multiple strategies:
     * 1. A simple route (Depot -> Customer -> Depot).
     * 2. A route with one optimal charging station (D->S->C->D or D->C->S->D).
     * 3. A route with two optimal charging stations (for very remote customers).
     * @return True if a feasible route was created and added to the solution, false otherwise.
     */
    bool createSmartNewRoute(int customerId,
                             Solution& solution,
                             const std::vector<int>& stationIds,
                             const std::shared_ptr<Instance>& instance);

} // namespace RepairUtils
