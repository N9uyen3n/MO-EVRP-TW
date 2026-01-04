#include "../../../../include/alns/operators/repair/GreedyDistanceInsertion.h"
#include "../../../../include/core/Solution.h"
#include "../../../../include/core/Route.h"
#include "../../../../include/core/Vehicle.h"
#include "../../../../include/core/Instance.h"
#include "../../../../include/core/Customer.h"
#include <algorithm>
#include <limits>
#include <vector>

namespace { // Anonymous namespace for file-local helper function
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

GreedyDistanceInsertion::GreedyDistanceInsertion(std::shared_ptr<Instance> inst)
    : instance(inst) {}

std::string GreedyDistanceInsertion::getName() const {
    return "Greedy Distance Insertion";
}

void GreedyDistanceInsertion::execute(Solution& solution, const std::vector<int>& unservedCustomers, std::mt19937& rng) {
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
            double estimatedCost;
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
        
        for (int i = 0; i < std::min(MAX_VERIFY, (int)candidates.size()); ++i) {
            auto& candidate = candidates[i];
            
            auto exactResult = routes[candidate.routeIdx].checkInsertionCost(
                customerId, 
                candidate.position
            );
            
            if (exactResult.isFeasible) {
                routes[candidate.routeIdx].addNode(customerId, candidate.position);
                routes[candidate.routeIdx].evaluate();
                inserted = true;
                break;
            }
        }
        
        if (!inserted) {
            createNewRouteForCustomer(solution, customerId, instance);
        }
    }
}