#include "operators/RepairOperators.h"
#include "Route.h"
#include <iostream>
#include <limits>
#include <algorithm>

// --- GreedyInsertion ---
bool GreedyInsertion::repair(Solution& solution, const Instance& instance, std::vector<std::shared_ptr<Customer>>& unassigned, std::mt19937& rng) {
    while (!unassigned.empty()) {
        double best_cost = std::numeric_limits<double>::max();
        std::shared_ptr<Customer> best_customer = nullptr;
        Route* best_route = nullptr;
        int best_position = -1;
        int unassigned_idx_to_remove = -1;

        for (int i = 0; i < unassigned.size(); ++i) {
            auto customer = unassigned[i];
            for (auto& route : solution.routes) {
                // A route is at least [depot, depot]. We can insert in between.
                for (int pos = 1; pos < route.getInfos().size(); ++pos) {
                    if (route.canInsert(customer, pos)) {
                        double cost = route.getInsertionCost(customer, pos);
                        if (cost < best_cost) {
                            best_cost = cost;
                            best_customer = customer;
                            best_route = &route;
                            best_position = pos;
                            unassigned_idx_to_remove = i;
                        }
                    }
                }
            }
        }

        if (best_customer) {
            best_route->insert(best_customer, best_position);
            unassigned.erase(unassigned.begin() + unassigned_idx_to_remove);
        } else {
            // No feasible insertion found for any remaining customer
            return false; 
        }
    }

    return true;
}

// --- RegretInsertion ---
bool RegretInsertion::repair(Solution& solution, const Instance& instance, std::vector<std::shared_ptr<Customer>>& unassigned, std::mt19937& rng) {
    while (!unassigned.empty()) {
        // For each customer, find k-best insertion positions
        struct CustomerRegret {
            std::shared_ptr<Customer> customer;
            double regret;
            double best_cost;
            Route* best_route;
            int best_position;
        };

        std::vector<CustomerRegret> regrets;

        for (const auto& cust : unassigned) {
            std::vector<double> costs;
            // Try all positions in all routes
            for (auto& route : solution.routes) {
                for (int pos = 1; pos < route.getInfos().size(); ++pos) {
                    if (route.canInsert(cust, pos)) {
                        costs.push_back(route.getInsertionCost(cust, pos));
                    }
                }
            }

            if (costs.empty()) continue;

            std::sort(costs.begin(), costs.end());
            
            double regret_value = 0.0;
            if (costs.size() > 1) {
                // Calculate regret between best and k-th best (or last if fewer than k)
                int limit = std::min((int)costs.size(), k);
                for (int i = 1; i < limit; ++i) {
                    regret_value += (costs[i] - costs[0]);
                }
            } else {
                regret_value = costs[0]; // High regret if only one option
            }

            regrets.push_back({cust, regret_value, costs[0], nullptr, -1});
        }

        if (regrets.empty()) return false; // No feasible insertion for any customer

        // Sort by regret (highest first = most constrained)
        std::sort(regrets.begin(), regrets.end(),
            [](const auto& a, const auto& b) { return a.regret > b.regret; });

        // Insert customer with highest regret at its best position
        auto& selected_customer_regret = regrets[0];
        auto cust_to_insert = selected_customer_regret.customer;

        // Find its best position again (could be cached, but this is safer)
        Route* best_route_for_insert = nullptr;
        int best_pos_for_insert = -1;
        double best_cost_for_insert = std::numeric_limits<double>::max();

        for (auto& route : solution.routes) {
            for (int pos = 1; pos < route.getInfos().size(); ++pos) {
                if (route.canInsert(cust_to_insert, pos)) {
                    double cost = route.getInsertionCost(cust_to_insert, pos);
                    if (cost < best_cost_for_insert) {
                        best_cost_for_insert = cost;
                        best_route_for_insert = &route;
                        best_pos_for_insert = pos;
                    }
                }
            }
        }

        if (!best_route_for_insert) return false; // Should not happen if regrets list was not empty

        best_route_for_insert->insert(cust_to_insert, best_pos_for_insert);
        unassigned.erase(std::remove(unassigned.begin(), unassigned.end(), cust_to_insert), unassigned.end());
    }

    return true;
}
