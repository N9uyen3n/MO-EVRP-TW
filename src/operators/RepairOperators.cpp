#include "../include/operators/RepairOperators.h"
#include "../include/Instance.h"
#include "../include/Customer.h"
#include "../include/Station.h"
#include "../include/Route.h"
#include <iostream>
#include <limits>
#include <algorithm>
#include <vector>

// Helper function to find the nearest station to a given node
static std::shared_ptr<Station> findNearestStation(const std::shared_ptr<Node>& fromNode, const std::vector<std::shared_ptr<Station>>& stations, const Instance& instance) {
    if (stations.empty()) {
        return nullptr;
    }
    double min_dist = std::numeric_limits<double>::max();
    std::shared_ptr<Station> nearest_station = nullptr;
    for (const auto& station : stations) {
        double dist = instance.getDistance(fromNode->getId(), station->getId());
        if (dist < min_dist) {
            min_dist = dist;
            nearest_station = station;
        }
    }
    return nearest_station;
}

// --- GreedyInsertion ---
bool GreedyInsertion::repair(Solution& solution, const Instance& instance, std::vector<std::shared_ptr<Customer>>& unassigned, std::mt19937& rng) {
    // --- DEBUG ---
    std::cout << "[DEBUG] GreedyRepair starting with " << unassigned.size() << " unassigned customers." << std::endl;
    // --- END DEBUG ---

    std::vector<std::shared_ptr<Customer>> customers_to_insert = unassigned;
    unassigned.clear();

    std::vector<std::shared_ptr<Station>> stations;
    for (const auto& node : instance.getNodes()) {
        if (auto s = std::dynamic_pointer_cast<Station>(node)) {
            stations.push_back(s);
        }
    }

    std::shuffle(customers_to_insert.begin(), customers_to_insert.end(), rng);

    for (const auto& customer : customers_to_insert) {
        // --- DEBUG ---
        std::cout << "  [DEBUG] GreedyRepair evaluating customer C" << customer->getId() << std::endl;
        // --- END DEBUG ---

        double best_cost_delta = std::numeric_limits<double>::max();
        Route* best_route = nullptr;
        int best_position = -1;
        bool requires_station = false;
        std::shared_ptr<Station> station_for_plan_b = nullptr;
        int station_pos = -1;
        
        int feasible_plan_a = 0; // DEBUG
        int feasible_plan_b = 0; // DEBUG

        for (auto& route : solution.routes) {
            for (int pos = 1; pos < route.getInfos().size(); ++pos) {
                // Plan A: Direct insertion
                EvaluationResult direct_result = route.evaluateInsertion(customer, pos);
                if (direct_result.isFeasible) {
                    feasible_plan_a++; // DEBUG
                    if (direct_result.costDelta < best_cost_delta) {
                        best_cost_delta = direct_result.costDelta;
                        best_route = &route;
                        best_position = pos;
                        requires_station = false;
                    }
                } else if (!direct_result.isFeasible) {
                    // Plan B: Try inserting a station
                    auto node_before = route.getInfos()[pos - 1].node;
                    auto station_to_try = findNearestStation(node_before, stations, instance);
                    if (station_to_try) {
                        Route tempRoute = route;
                        EvaluationResult station_result = tempRoute.evaluateInsertion(station_to_try, pos);
                        if (station_result.isFeasible) {
                            tempRoute.insert(station_to_try, pos);
                            EvaluationResult customer_result = tempRoute.evaluateInsertion(customer, pos + 1);
                            if (customer_result.isFeasible) {
                                feasible_plan_b++; // DEBUG
                                double combined_cost = station_result.costDelta + customer_result.costDelta;
                                if (combined_cost < best_cost_delta) {
                                    best_cost_delta = combined_cost;
                                    best_route = &route;
                                    best_position = pos + 1;
                                    requires_station = true;
                                    station_for_plan_b = station_to_try;
                                    station_pos = pos;
                                }
                            }
                        }
                    }
                }
            }
        }
        
        // --- DEBUG ---
        std::cout << "    [DEBUG] C" << customer->getId() << ": Found " << feasible_plan_a << " Plan A spots, " << feasible_plan_b << " Plan B spots." << std::endl;
        // --- END DEBUG ---

        if (best_route) {
            // --- DEBUG ---
            std::cout << "    [DEBUG] C" << customer->getId() << " inserted successfully." << std::endl;
            // --- END DEBUG ---
            if (requires_station) {
                best_route->insert(station_for_plan_b, station_pos);
                best_route->insert(customer, best_position);
            } else {
                best_route->insert(customer, best_position);
            }
        } else {
            // --- DEBUG ---
            std::cout << "    [DEBUG] C" << customer->getId() << " FAILED TO INSERT. Adding to unassigned." << std::endl;
            // --- END DEBUG ---
            unassigned.push_back(customer);
        }
    }
    
    // --- DEBUG ---
    std::cout << "[DEBUG] GreedyRepair finished. " << unassigned.size() << " customers remain unassigned." << std::endl;
    // --- END DEBUG ---

    return unassigned.empty();
}


// --- RegretInsertion ---
bool RegretInsertion::repair(Solution& solution, const Instance& instance, std::vector<std::shared_ptr<Customer>>& unassigned, std::mt19937& rng) {
    // --- DEBUG ---
    std::cout << "[DEBUG] RegretRepair starting with " << unassigned.size() << " unassigned customers." << std::endl;
    // --- END DEBUG ---
    
    std::vector<std::shared_ptr<Station>> stations;
    for (const auto& node : instance.getNodes()) {
        if (auto s = std::dynamic_pointer_cast<Station>(node)) {
            stations.push_back(s);
        }
    }

    while (!unassigned.empty()) {
        std::shared_ptr<Customer> best_customer_to_insert = nullptr;
        double max_regret = -1.0;
        
        struct BestInsertionAction {
            Route* route;
            int position;
            bool requires_station;
            std::shared_ptr<Station> station;
            int station_pos;
        } best_action;

        auto customer_it = unassigned.begin();
        while (customer_it != unassigned.end()) {
            const auto& customer = *customer_it;
            
            struct InsertionInfo {
                double costDelta;
                Route* route;
                int position;
                bool requires_station;
                std::shared_ptr<Station> station;
                int station_pos;
            };
            std::vector<InsertionInfo> insertions;
            
            int feasible_plan_a = 0; // DEBUG
            int feasible_plan_b = 0; // DEBUG

            for (auto& route : solution.routes) {
                for (int pos = 1; pos < route.getInfos().size(); ++pos) {
                    // Plan A
                    EvaluationResult direct_result = route.evaluateInsertion(customer, pos);
                    if (direct_result.isFeasible) {
                        feasible_plan_a++; // DEBUG
                        insertions.push_back({direct_result.costDelta, &route, pos, false, nullptr, -1});
                    } else {
                        // Plan B
                        auto node_before = route.getInfos()[pos - 1].node;
                        auto station_to_try = findNearestStation(node_before, stations, instance);
                        if (station_to_try) {
                            Route tempRoute = route;
                            EvaluationResult station_result = tempRoute.evaluateInsertion(station_to_try, pos);
                            if (station_result.isFeasible) {
                                tempRoute.insert(station_to_try, pos);
                                EvaluationResult customer_result = tempRoute.evaluateInsertion(customer, pos + 1);
                                if (customer_result.isFeasible) {
                                    feasible_plan_b++; // DEBUG
                                    Route tempRoute2 = tempRoute;
                                    tempRoute2.insert(customer, pos + 1);
                                    double total_delta = tempRoute2.getTotalTime() - route.getTotalTime();
                                    insertions.push_back({total_delta, &route, pos + 1, true, station_to_try, pos});
                                }
                            }
                        }
                    }
                }
            }
            
            // --- DEBUG ---
            std::cout << "    [DEBUG] RegretRepair evaluating C" << customer->getId() << ": Found " << feasible_plan_a << " (A) spots, " << feasible_plan_b << " (B) spots. Total insertions: " << insertions.size() << std::endl;
            // --- END DEBUG ---

            if (insertions.empty()) {
                ++customer_it;
                continue; 
            }

            std::sort(insertions.begin(), insertions.end(), [](const auto& a, const auto& b) {
                return a.costDelta < b.costDelta;
            });

            double regret = 0.0;
            if (insertions.size() > 1) {
                int limit = std::min((int)insertions.size(), k);
                for (int i = 1; i < limit; ++i) {
                    regret += (insertions[i].costDelta - insertions[0].costDelta);
                }
            } else {
                regret = std::numeric_limits<double>::max() / 2;
            }

            if (regret > max_regret) {
                max_regret = regret;
                best_customer_to_insert = customer;
                best_action = {insertions[0].route, insertions[0].position, insertions[0].requires_station, insertions[0].station, insertions[0].station_pos};
            }
            ++customer_it;
        }

        if (best_customer_to_insert) {
            // --- DEBUG ---
            std::cout << "  [DEBUG] RegretRepair inserting C" << best_customer_to_insert->getId() << " (Max Regret)" << std::endl;
            // --- END DEBUG ---
            if (best_action.requires_station) {
                best_action.route->insert(best_action.station, best_action.station_pos);
            }
            best_action.route->insert(best_customer_to_insert, best_action.position);
            
            unassigned.erase(std::remove(unassigned.begin(), unassigned.end(), best_customer_to_insert), unassigned.end());
        } else {
            // --- DEBUG ---
            std::cout << "[DEBUG] RegretRepair FAILED. No best customer found. " << unassigned.size() << " customers remain." << std::endl;
            // --- END DEBUG ---
            return false;
        }
    }

    std::cout << "[DEBUG] RegretRepair finished successfully." << std::endl;
    return true;
}