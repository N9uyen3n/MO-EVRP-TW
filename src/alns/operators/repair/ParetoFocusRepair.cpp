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

// Helper function
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

void ParetoFocusRepair::execute(Solution& solution, const std::vector<int>& unservedCustomers, std::mt19937& rng) {
    std::uniform_int_distribution<> distObj(0, 2);
    int focusObj = distObj(rng);

    std::vector<int> customers = unservedCustomers;
    std::shuffle(customers.begin(), customers.end(), rng);

    auto& routes = solution.getRoutes();

    double meanRouteDuration = 0.0;
    if (focusObj == 2 && !routes.empty()) {
        double totalDuration = 0;
        int activeRoutes = 0;
        for(const auto& route : routes) {
            if (!route.getCustomers().empty()) {
                totalDuration += route.getTotalTime();
                activeRoutes++;
            }
        }
        meanRouteDuration = activeRoutes > 0 ? totalDuration / activeRoutes : 0.0;
    }

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
            double estimatedCost;
            bool operator<(const Candidate& other) const {
                return estimatedCost < other.estimatedCost;
            }
        };
        
        std::vector<Candidate> candidates;
        
        for (int r : feasibleRoutesIndices) {
            for (size_t pos = 1; pos < routes[r].getNodes().size(); ++pos) {
                if (!routes[r].canPossiblyInsert(customerId, pos)) {
                    continue;
                }
                
                auto fastResult = routes[r].fastForwardCheck(customerId, pos);
                
                if (fastResult.isFeasible) {
                    double estimatedCost = 0.0;
                    if (focusObj == 0) { // Distance
                        estimatedCost = fastResult.deltaDistance;
                    } else { // Time or Workload - use distance as proxy
                        estimatedCost = fastResult.deltaDistance;
                    }
                    candidates.push_back({r, pos, estimatedCost});
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
                double currentExactCost = 0.0;
                if (focusObj == 0) { // Distance
                    currentExactCost = exactResult.deltaDistance;
                } else if (focusObj == 1) { // Time
                    currentExactCost = exactResult.deltaTime;
                } else { // Workload
                    double newRouteTime = routes[candidate.routeIdx].getTotalTime() + exactResult.deltaTime;
                    currentExactCost = std::abs(newRouteTime - meanRouteDuration);
                }

                if (currentExactCost < minExactCost) {
                    minExactCost = currentExactCost;
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
