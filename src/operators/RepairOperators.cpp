#include "../include/operators/RepairOperators.h"
#include "../include/Instance.h"
#include "../include/Customer.h"
#include "../include/Route.h"
#include <iostream>
#include <limits>
#include <algorithm>
#include <vector>

// --- GreedyInsertion ---
bool GreedyInsertion::repair(Solution& solution, const Instance& instance, std::vector<std::shared_ptr<Customer>>& unassigned, std::mt19937& rng) {
    std::vector<std::shared_ptr<Customer>> customers_to_insert = unassigned;
    unassigned.clear();

    // Randomize the order of customers to be inserted
    std::shuffle(customers_to_insert.begin(), customers_to_insert.end(), rng);

    for (const auto& customer : customers_to_insert) {
        double best_cost_delta = std::numeric_limits<double>::max();
        Route* best_route = nullptr;
        int best_position = -1;

        for (auto& route : solution.routes) {
            // A route is at least [depot, depot]. We can insert in between.
            for (int pos = 1; pos < route.getInfos().size(); ++pos) {
                EvaluationResult result = route.evaluateInsertion(customer, pos);
                if (result.isFeasible) {
                    if (result.costDelta < best_cost_delta) {
                        best_cost_delta = result.costDelta;
                        best_route = &route;
                        best_position = pos;
                    }
                }
            }
        }

        if (best_route) {
            best_route->insert(customer, best_position);
        } else {
            // If no feasible insertion is found, add customer back to unassigned
            unassigned.push_back(customer);
        }
    }

    // Return true if all customers were inserted, false otherwise
    return unassigned.empty();
}


// --- RegretInsertion ---
bool RegretInsertion::repair(Solution& solution, const Instance& instance, std::vector<std::shared_ptr<Customer>>& unassigned, std::mt19937& rng) {
    while (!unassigned.empty()) {
        std::shared_ptr<Customer> best_customer_to_insert = nullptr;
        double max_regret = -1.0;
        Route* best_route_for_best_customer = nullptr;
        int best_pos_for_best_customer = -1;

        auto it = unassigned.begin();
        while (it != unassigned.end()) {
            const auto& customer = *it;
            struct InsertionInfo {
                double costDelta;
                Route* route;
                int position;
            };
            std::vector<InsertionInfo> insertions;

            for (auto& route : solution.routes) {
                for (int pos = 1; pos < route.getInfos().size(); ++pos) {
                    EvaluationResult result = route.evaluateInsertion(customer, pos);
                    if (result.isFeasible) {
                        insertions.push_back({result.costDelta, &route, pos});
                    }
                }
            }

            if (insertions.empty()) {
                ++it;
                continue; // This customer cannot be inserted anywhere, check next
            }

            // Sort insertions by costDelta
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
                // If only one insertion is possible, it's very important.
                // We can assign a high regret. A simple approach is to use a large value 
                // or a value related to its own cost, to prioritize it.
                regret = std::numeric_limits<double>::max() / 2; // A large value to prioritize
            }


            if (regret > max_regret) {
                max_regret = regret;
                best_customer_to_insert = customer;
                best_route_for_best_customer = insertions[0].route;
                best_pos_for_best_customer = insertions[0].position;
            }
            ++it;
        }

        if (best_customer_to_insert) {
            best_route_for_best_customer->insert(best_customer_to_insert, best_pos_for_best_customer);
            // Remove the inserted customer from unassigned list
            unassigned.erase(std::remove(unassigned.begin(), unassigned.end(), best_customer_to_insert), unassigned.end());
        } else {
            // No customer could be inserted, stop the repair process
            return false;
        }
    }

    return true;
}
