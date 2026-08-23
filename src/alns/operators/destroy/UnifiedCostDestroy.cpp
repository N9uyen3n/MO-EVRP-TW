#include "../../../../include/alns/operators/destroy/UnifiedCostDestroy.h"
#include "../../../../include/core/Customer.h"
#include "../../../../include/core/Station.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <unordered_set>

UnifiedCostDestroy::UnifiedCostDestroy(std::shared_ptr<Instance> instance, int determinism_param)
    : instance(instance), determinism(determinism_param) {}

std::string UnifiedCostDestroy::getName() const {
    return "Unified Cost Destroy";
}

std::vector<int> UnifiedCostDestroy::execute(Solution &solution, int nodesToRemove, std::mt19937 &rng) {
    std::vector<int> removedCustomers;
    auto &routes = solution.getRoutes();

    if (routes.empty()) {
        return removedCustomers;
    }

    // 1. Select a random metric for this execution
    std::uniform_int_distribution<> dist(0, 3);
    CostMetric metric = static_cast<CostMetric>(dist(rng));

    // For WORKLOAD metric, calculate average route duration
    double avgRouteDuration = 0.0;
    if (metric == WORKLOAD) {
        double totalDuration = 0.0;
        int activeRoutes = 0;
        for (const auto &route : routes) {
            if (route.size() > 2) {
                totalDuration += route.getTotalTime();
                activeRoutes++;
            }
        }
        if (activeRoutes > 0) {
            avgRouteDuration = totalDuration / activeRoutes;
        }
    }

    // 2. Calculate savings for all candidate nodes
    std::vector<RemovalCandidate> candidates;

    for (const auto &route : routes) {
        if (route.size() <= 2)
            continue;

        // Route quality bonus (only applied for DISTANCE/ENERGY/TIME metrics)
        double routeBonus = 0.0;
        if (metric != WORKLOAD) {
            routeBonus = getRouteQualityBonus(route);
        }

        const auto &nodes = route.getNodes();
        // Iterate through customers (skipping depot start/end)
        for (size_t i = 1; i < nodes.size() - 1; ++i) {
            int nodeId = nodes[i];

            // Skip stations, only remove customers
            if (instance->getNodeById(nodeId)->getType() != NodeType::CUSTOMER) {
                continue;
            }

            double baseSaving = calculateNodeSaving(route, i, metric, avgRouteDuration);

            // Add Slack Bonus (easier to reinsert nodes with large TW slack)
            // Only apply for "Cost" metrics, usually helps finding better spots
            double slackBonus = 0.0;
            if (metric != WORKLOAD) {
                slackBonus = getTimeWindowSlack(nodeId, route) * 2.0; // Weight of 2.0
            }

            double totalScore = baseSaving + routeBonus + slackBonus;
            candidates.push_back({nodeId, totalScore});
        }
    }

    // 3. Sort candidates by savings (descending)
    std::sort(candidates.begin(), candidates.end(),
              std::greater<RemovalCandidate>());

    // 4. Randomized Greedy Selection
    while (removedCustomers.size() < nodesToRemove && !candidates.empty()) {
        std::uniform_real_distribution<double> realDist(0.0, 1.0);
        double r = realDist(rng);

        // Select a candidate with probability proportional to its score
        double cumulativeProb = 0.0;
        for (const auto &candidate : candidates) {
            cumulativeProb += candidate.savings;
            if (r <= cumulativeProb) {
                removedCustomers.push_back(candidate.nodeId);
                candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                    [&candidate = candidate](const RemovalCandidate &c) {
                        return c.nodeId == candidate.nodeId;
                    }),
                    candidates.end());
                break;
            }
        }
    }

    // 5. Remove customers from routes and update solution
    std::unordered_set<int> removedSet(removedCustomers.begin(), removedCustomers.end());
    std::unordered_set<int> stationsToRemove;

    // First pass: mark customers and stations for removal
    for (auto &route : routes) {
        const auto &nodes = route.getNodes();
        for (size_t i = 1; i < nodes.size() - 1; ++i) {
            if (removedSet.find(nodes[i]) == removedSet.end())
                continue;

            // RCWPS: Check preceding node (i-1) is a station, skip if i-1 == 0 (depot)
            if (i > 1 &&
                instance->getNodeById(nodes[i - 1])->getType() == NodeType::STATION) {
                // Check if station's OTHER neighbor (i-2) is a non-removed customer
                // If so, station may serve that customer → don't remove
                bool safeToRemove = true;
                if (i > 2) {
                    auto otherNeighbor = instance->getNodeById(nodes[i - 2]);
                    if (otherNeighbor->getType() == NodeType::CUSTOMER &&
                        removedSet.find(nodes[i - 2]) == removedSet.end()) {
                        safeToRemove = false; // Station may serve i-2, keep it
                    }
                }
                if (safeToRemove && std::uniform_int_distribution<>(0, 1)(rng) < 0.5) {
                    stationsToRemove.insert(nodes[i - 1]);
                }
            }

            // RCWSS: Check succeeding node (i+1) is a station, skip if last node (depot)
            if (i + 1 < nodes.size() - 1 &&
                instance->getNodeById(nodes[i + 1])->getType() == NodeType::STATION) {
                // Check if station's OTHER neighbor (i+2) is a non-removed customer
                bool safeToRemove = true;
                if (i + 2 < nodes.size() - 1) {
                    auto otherNeighbor = instance->getNodeById(nodes[i + 2]);
                    if (otherNeighbor->getType() == NodeType::CUSTOMER &&
                        removedSet.find(nodes[i + 2]) == removedSet.end()) {
                        safeToRemove = false; // Station may serve i+2, keep it
                    }
                }
                if (safeToRemove && std::uniform_int_distribution<>(0, 1)(rng) < 0.5) {
                    stationsToRemove.insert(nodes[i + 1]);
                }
            }
        }
    }

    // Merge both sets for unified removal
    std::unordered_set<int> allRemovedSet = removedSet;
    allRemovedSet.insert(stationsToRemove.begin(), stationsToRemove.end());

    // Second pass: rebuild routes without removed nodes
    for (auto &route : routes) {
        bool changed = false;
        std::vector<int> newSequence;
        const auto &nodes = route.getNodes();

        newSequence.push_back(nodes[0]); // Depot start

        for (size_t i = 1; i < nodes.size() - 1; ++i) {
            int nodeId = nodes[i];

            if (allRemovedSet.find(nodeId) == allRemovedSet.end()) {
                newSequence.push_back(nodeId);
            } else {
                changed = true;
            }
        }
        newSequence.push_back(nodes.back()); // Depot end

        if (changed) {
            route.clear();
            for (size_t i = 1; i < newSequence.size() - 1; ++i) {
                route.addNode(newSequence[i], i);
            }
            route.evaluate();
        }
    }

    return removedCustomers;
}

double UnifiedCostDestroy::calculateNodeSaving(const Route &route, size_t position, CostMetric metric, double avgRouteDuration) {
    const auto &nodes = route.getNodes();
    int prev = nodes[position - 1];
    int curr = nodes[position];
    int next = nodes[position + 1];

    if (metric == DISTANCE) {
        double distWith = instance->getDistance(prev, curr) + instance->getDistance(curr, next);
        double distWithout = instance->getDistance(prev, next);
        return distWith - distWithout;
    } else if (metric == TIME) {
        // Includes travel time + service time
        double timeWith = instance->getTime(prev, curr) + instance->getTime(curr, next);
        timeWith += instance->getNodeById(curr)->getServiceTime();
        double timeWithout = instance->getTime(prev, next);
        return timeWith - timeWithout;
    } else if (metric == ENERGY) {
        // Energy ~ Distance * Rate
        double distWith = instance->getDistance(prev, curr) + instance->getDistance(curr, next);
        double distWithout = instance->getDistance(prev, next);
        double savingDist = distWith - distWithout;
        return savingDist * route.getVehicle()->getEnergyConsumptionRate();
    } else if (metric == WORKLOAD) {
        // For workload, we want to remove nodes from routes that are FAR from the
        // average duration. If route is too long (> avg), removing node reduces
        // duration -> Good (High Score) If route is too short (< avg), removing
        // node makes it shorter -> Bad (Low Score)

        double currentDuration = route.getTotalTime();
        double diff = currentDuration - avgRouteDuration;

        // If route is longer than average, diff > 0. We want to remove nodes from
        // here. Impact is roughly proportional to the time saving of that node.

        double timeSaving = instance->getTime(prev, curr) +
            instance->getTime(curr, next) +
            instance->getNodeById(curr)->getServiceTime() -
            instance->getTime(prev, next);

        if (diff > 0 && timeSaving > 0) {
            return timeSaving; // Helps reduce the overload
        } else if (diff <= 0 && timeSaving > 0) {
            return -timeSaving; // Penalize removing from underloaded routes
        } else {
            return 0.0; // timeSaving <= 0 means removing it increases time, don't
                        // reward
        }
    }

    return 0.0;
}

double UnifiedCostDestroy::getRouteQualityBonus(const Route &route) {
    double bonus = 0.0;

    if (route.size() <= 2)
        return bonus;

    // 1. Station penalty (routes with many stations are "bad" candidates to keep)
    int stationCount = 0;
    for (int nodeId : route.getNodes()) {
        if (instance->getNodeById(nodeId)->getType() == NodeType::STATION) {
            stationCount++;
        }
    }
    if (stationCount > 2) {
        bonus += 5.0 * (stationCount - 2);
    }

    // 2. Length bonus (very long routes might need pruning)
    int customerCount = 0;
    for (int nodeId : route.getNodes()) {
        if (instance->getNodeById(nodeId)->getType() == NodeType::CUSTOMER) {
            customerCount++;
        }
    }
    if (customerCount > 10) {
        bonus += 3.0;
    }

    return bonus;
}

double UnifiedCostDestroy::getTimeWindowSlack(int customerId, const Route &route) {
    const auto &states = route.getStates();
    const auto &nodes = route.getNodes();

    for (size_t i = 1; i < nodes.size() - 1; ++i) {
        if (nodes[i] == customerId) {
            auto customer = instance->getNodeById(customerId);
            double arrivalTime = states[i].arrivalTime;
            double readyTime = customer->getReadyTime();
            double dueDate = customer->getDueDate();

            double width = dueDate - readyTime;
            if (width <= 1e-6)
                return 0.0;

            // Slack: Relative position in TW.
            // Closer to ReadyTime (Early arrival) means more slack for delay.
            // Closer to DueDate means tight.
            double slack = (dueDate - arrivalTime) / width;
            return std::max(0.0, std::min(1.0, slack));
        }
    }
    return 0.0;
}

// Station-relatedness scoring implementation
std::unordered_set<int> UnifiedCostDestroy::getStationNeighbors(const Route& route, int customerId) const {
    std::unordered_set<int> stations;
    const auto& nodes = route.getNodes();

    for (size_t i = 1; i < nodes.size() - 1; ++i) {
        if (nodes[i] == customerId) {
            // Check previous node
            if (i > 1 && instance->getNodeById(nodes[i-1])->getType() == NodeType::STATION) {
                stations.insert(nodes[i-1]);
            }
            // Check next node
            if (i < nodes.size() - 2 && instance->getNodeById(nodes[i+1])->getType() == NodeType::STATION) {
                stations.insert(nodes[i+1]);
            }
        }
    }

    return stations;
}

double UnifiedCostDestroy::calculateStationRelatedness(const Route& route, int customerId) const {
    // Find all stations neighboring this customer
    auto stations = getStationNeighbors(route, customerId);
    if (stations.empty()) return 0.0;

    // Calculate average distance to neighboring stations
    double totalDist = 0.0;
    int count = 0;

    for (int stationId : stations) {
        totalDist += instance->getDistance(customerId, stationId);
        count++;
    }

    double avgStationDist = totalDist / count;

    // Calculate average distance to all stations in the instance
    double totalInstanceStationDist = 0.0;
    int instanceStationCount = 0;
    int depotId = 0;

    for (const auto& node : instance->getNodes()) {
        if (node->getType() == NodeType::STATION) {
            totalInstanceStationDist += instance->getDistance(depotId, node->getId());
            instanceStationCount++;
        }
    }

    double avgInstanceStationDist = instanceStationCount > 0 ? totalInstanceStationDist / instanceStationCount : 0.0;

    // Relatedness score: lower distance = higher relatedness
    // Normalize by instance average station distance
    if (avgInstanceStationDist > 0) {
        return std::max(0.0, 1.0 - (avgStationDist / avgInstanceStationDist));
    }

    return 0.0;
}