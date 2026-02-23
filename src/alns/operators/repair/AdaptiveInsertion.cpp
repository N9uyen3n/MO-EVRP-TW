#include "../../../../include/alns/operators/repair/AdaptiveInsertion.h"
#include <algorithm>
#include <iostream>
#include <random>

namespace alns {

AdaptiveInsertion::AdaptiveInsertion(std::shared_ptr<Instance> instance)
    : instance(instance) {}

std::string AdaptiveInsertion::getName() const {
    return "Adaptive Insertion";
}

void AdaptiveInsertion::execute(Solution& solution, const std::vector<int>& unservedCustomers, std::mt19937& rng) {
    // 1. Select a mode for this execution (33% each)
    std::uniform_real_distribution<> dist(0.0, 1.0);
    double r = dist(rng);
    
    Mode mode;
    if (r < 0.33) mode = Mode::DISTANCE_FOCUSED;
    else if (r < 0.66) mode = Mode::WORKLOAD_FOCUSED;
    else mode = Mode::MAXTIME_FOCUSED;

    std::vector<int> customers = unservedCustomers;
    std::shuffle(customers.begin(), customers.end(), rng);

    auto& routes = solution.getRoutes();

    for (int customerId : customers) {
        InsertionCandidate bestInsertion = {-1, -1, -1, std::numeric_limits<double>::infinity()};
        
        // --- Calculate Current State (For Gini/MaxTime Modes) ---
        double meanRouteDuration = 0.0;
        double maxRouteDuration = 0.0;
        if (mode != Mode::DISTANCE_FOCUSED && !routes.empty()) {
            double totalDuration = 0.0;
            for (const auto& route : routes) {
                double duration = route.getTotalTime();
                totalDuration += duration;
                if (duration > maxRouteDuration) {
                    maxRouteDuration = duration;
                }
            }
            meanRouteDuration = totalDuration / routes.size();
        }

        // 2. Find best insertion for this customer
        for (size_t r = 0; r < routes.size(); ++r) {
            auto& route = routes[r];
            const auto& nodes = route.getNodes();

            // Optimization: Skip full check if capacity fails (Tier 1)
            double demand = instance->getNodeById(customerId)->getDemand(); 
            if (!route.quickCapacityCheck(demand)) continue;

            for (size_t i = 1; i < nodes.size(); ++i) {
                // Tier 2: Pre-check feasibility
                if (!route.canPossiblyInsert(customerId, i)) continue;

                // Tier 3: Check insertion cost
                auto result = route.checkInsertionCost(customerId, i);
                
                if (result.isFeasible) {
                    double cost = calculateCost(result, mode, route, meanRouteDuration, maxRouteDuration, customerId);
                    if (cost < bestInsertion.cost) {
                        bestInsertion = {customerId, static_cast<int>(r), static_cast<int>(i), cost};
                    }
                }
            }
        }

        // 3. Apply insertion
        if (bestInsertion.routeIndex != -1) {
            auto& route = routes[bestInsertion.routeIndex];
            route.addNode(customerId, bestInsertion.position);
            route.evaluate();
        } else {
            // 4. Create new route
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

double AdaptiveInsertion::calculateCost(const InsertionResult& result, Mode mode, const Route& route, double meanRouteDuration, double maxRouteDuration, int customerId) const {
    switch (mode) {
        case Mode::DISTANCE_FOCUSED:
            return result.deltaDistance;
            
        case Mode::WORKLOAD_FOCUSED: {
            double serviceTime = instance->getNodeById(customerId)->getServiceTime();
            // Estimate new duration of this route
            double newRouteTime = route.getTotalTime() + result.deltaWaitTime + result.deltaDistance + serviceTime;
            // Best cost = route time gets closest to the mean
            return std::abs(newRouteTime - meanRouteDuration);
        }
            
        case Mode::MAXTIME_FOCUSED: {
            double serviceTime = instance->getNodeById(customerId)->getServiceTime();
            double newRouteTime = route.getTotalTime() + result.deltaWaitTime + result.deltaDistance + serviceTime;
            
            double cost = result.deltaDistance;
            
            // Severe penalty if this insertion makes the route the NEW longest route
            // or pushes it too close to the existing max duration
            if (maxRouteDuration > 0) {
                if (newRouteTime > maxRouteDuration) {
                    cost += 10000.0 * (newRouteTime - maxRouteDuration); // Extreme penalty
                } else if (newRouteTime > 0.9 * maxRouteDuration) {
                    cost += 500.0; // Warning penalty
                }
            }
            return cost;
        }
            
        default:
            return result.deltaDistance;
    }
}

} // namespace alns
