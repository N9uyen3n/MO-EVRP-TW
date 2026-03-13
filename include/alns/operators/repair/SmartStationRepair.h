#pragma once
#include "../../IOperator.h"
#include "../../../core/Instance.h"
#include "../../../core/Solution.h"
#include <limits>
#include <memory>
#include <vector>
#include <random>

namespace alns {

/**
 * @brief Smart Station Repair Operator
 * 
 * An enhanced version of GreedyStationRepair that:
 * 1. Prioritizes inserting customers without stations first.
 * 2. If infeasible due to energy, intelligently searches for the best station insertion.
 * 3. Prevents "station loops" (redundant visits to the same station).
 * 4. Optimizes station selection based on detour cost, not just proximity.
 */
class SmartStationRepair : public IRepairOperator {
public:
    explicit SmartStationRepair(std::shared_ptr<Instance> instance);

    std::string getName() const override;

    void execute(Solution& solution, const std::vector<int>& unservedCustomers, std::mt19937& rng) override;

private:
    std::shared_ptr<Instance> instance;

    struct InsertionCandidate {
        int customerId = -1;
        int routeIndex = -1;
        int position = -1; // Position to insert customer
        double costIncrease = std::numeric_limits<double>::infinity();
        
        // Station details (if needed)
        bool requiresStation = false;
        int stationId = -1;
        int stationPosition = -1; // Position to insert station
    };

    /**
     * @brief Finds the best insertion for a customer, potentially adding a station.
     */
    InsertionCandidate findBestInsertion(int customerId, const Solution& solution);

    /**
     * @brief Evaluates inserting a customer (and optionally a station) into a route.
     * Returns cost increase or infinity if infeasible.
     */
    double evaluateInsertionWithStation(const Route& route, int customerId, int index, int stationId, int stationIndex);
};

} // namespace alns
