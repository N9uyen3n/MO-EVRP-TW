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
    return "Greedy Energy Insertion (Enhanced)";
}


// Helper function from GreedyDistanceInsertion, assuming it's moved to a common place or duplicated.
namespace {
void createNewRouteForCustomer(Solution& solution, int customerId, std::shared_ptr<Instance> instance) {
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

void GreedyEnergyInsertion::execute(Solution& solution, const std::vector<int>& unservedCustomers, std::mt19937& rng) {
    std::vector<int> customers = unservedCustomers;
    std::shuffle(customers.begin(), customers.end(), rng);

    auto& routes = solution.getRoutes();

    for (int customerId : customers) {
        
        auto customerNode = instance->getNodeById(customerId);
        double customerDemand = std::static_pointer_cast<Customer>(customerNode)->getDemand();
        
        std::vector<int> feasibleRoutesIndices;
        for (int r = 0; r < routes.size(); ++r) {
            if (routes[r].quickCapacityCheck(customerDemand)) {
                feasibleRoutesIndices.push_back(r);
            }
        }
        
        if (feasibleRoutesIndices.empty()) {
            createNewRouteForCustomer(solution, customerId, instance);
            continue;
        }

        struct Candidate {
            int routeIdx;
            size_t position;
            double estimatedCost; // For Energy, we use deltaDistance as a proxy
            bool operator<(const Candidate& other) const {
                return estimatedCost < other.estimatedCost;
            }
        };
        
        std::vector<Candidate> candidates;
        
        for (int r : feasibleRoutesIndices) {
            const auto& nodes = routes[r].getNodes();
            for (size_t pos = 1; pos < nodes.size(); ++pos) {
                
                if (!routes[r].canPossiblyInsert(customerId, pos)) {
                    continue;
                }
                
                auto fastResult = routes[r].fastForwardCheck(customerId, pos);
                
                if (fastResult.isFeasible) {
                    // NOTE: The true cost is deltaChargeAmount, but fastForwardCheck doesn't calculate it.
                    // We use deltaDistance as a proxy to find promising candidates.
                    // The final verification will use the true cost.
                    candidates.push_back({r, pos, fastResult.deltaDistance});
                }
            }
        }
        
        if (candidates.empty()) {
            createNewRouteForCustomer(solution, customerId, instance);
            continue;
        }
        
        std::sort(candidates.begin(), candidates.end());
        
        const int MAX_VERIFY = 3;
        bool inserted = false;
        
        // We need to find the best among the verified candidates, not just the first one.
        double minExactCost = std::numeric_limits<double>::max();
        int bestRouteIdx = -1;
        size_t bestPos = -1;

        for (int i = 0; i < std::min(MAX_VERIFY, (int)candidates.size()); ++i) {
            auto& candidate = candidates[i];
            
            auto exactResult = routes[candidate.routeIdx].checkInsertionCost(
                customerId, 
                candidate.position
            );
            
            if (exactResult.isFeasible) {
                // ⭐ FIX: True energy cost calculation
                // Cost = Energy consumed + Time cost of charging + Penalty for new stations
                
                // 1. Energy consumption (always present when traveling)
                double energyConsumption = exactResult.deltaEnergyConsumption;
                
                // 2. Charging time cost (if charging is needed)
                double chargingTimeCost = exactResult.deltaChargeAmount > 1e-6 
                                        ? exactResult.deltaChargeAmount * 0.5  // Charging time penalty
                                        : 0.0;
                
                // 3. Station penalty (discourage adding new stations)
                // Note: We don't have direct info about new stations in InsertionResult
                // So we use deltaChargeAmount as proxy - if > 0, likely added station
                double stationPenalty = exactResult.deltaChargeAmount > 1e-6 
                                      ? 10.0  // Penalty for requiring charging
                                      : 0.0;
                
                // Total energy cost (NO fallback to distance!)
                double energyCost = 1.0 * energyConsumption      // Primary: energy consumed
                                  + 0.5 * chargingTimeCost       // Secondary: time cost
                                  + 0.3 * stationPenalty;        // Tertiary: station penalty

                if (energyCost < minExactCost) {
                    minExactCost = energyCost;
                    bestRouteIdx = candidate.routeIdx;
                    bestPos = candidate.position;
                    inserted = true;
                }
            }
        }
        
        if (inserted) {
            routes[bestRouteIdx].addNode(customerId, bestPos);
            routes[bestRouteIdx].evaluate();
        } else {
            createNewRouteForCustomer(solution, customerId, instance);
        }
    }
}