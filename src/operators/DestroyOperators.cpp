#include "operators/DestroyOperators.h"
#include "Route.h"
#include "Customer.h"
// #include "../core/ServiceLocator.h" // Removed
#include <iostream>
#include <algorithm> // For std::shuffle, std::sort

// --- RandomRemoval ---
std::vector<std::shared_ptr<Customer>> RandomRemoval::destroy(Solution& solution, const Instance& instance, double rate, std::mt19937& rng) {
    std::vector<std::shared_ptr<Customer>> removed_customers;
    std::vector<std::shared_ptr<Customer>> all_customers = solution.getAllCustomers();

    if (all_customers.empty()) {
        return removed_customers;
    }

    std::shuffle(all_customers.begin(), all_customers.end(), rng);

    int toRemove = static_cast<int>(all_customers.size() * rate);
    if (toRemove == 0 && !all_customers.empty()) {
        toRemove = 1;
    }

    for (int i = 0; i < toRemove; ++i) {
        auto customer_to_remove = all_customers[i];
        for (auto& route : solution.routes) {
            if (route.removeCustomer(customer_to_remove)) {
                removed_customers.push_back(customer_to_remove);
                break;
            }
        }
    }

    return removed_customers;
}


// --- WorstDistanceRemoval ---
std::vector<std::shared_ptr<Customer>> WorstDistanceRemoval::destroy(Solution& solution, const Instance& instance, double rate, std::mt19937& rng) {
    std::vector<std::shared_ptr<Customer>> removed_customers;
    // auto instance = ServiceLocator::getService<Instance>(); // Removed

    struct CustomerCost {
        std::shared_ptr<Customer> customer;
        Route* route;
        int position;
        double saving;
    };

    std::vector<CustomerCost> costs;

    for (auto& route : solution.routes) {
        if (route.getInfos().size() <= 2) continue; // Skip if only depots

        for (int i = 1; i < route.getInfos().size() - 1; ++i) {
            auto cust = std::dynamic_pointer_cast<Customer>(route.getInfos()[i].node);
            if (!cust) continue;

            auto prev_node = route.getInfos()[i-1].node;
            auto next_node = route.getInfos()[i+1].node;

            double current_dist = instance.getDistance(prev_node->getId(), cust->getId()) + instance.getDistance(cust->getId(), next_node->getId());
            double new_dist = instance.getDistance(prev_node->getId(), next_node->getId());
            double saving = current_dist - new_dist;

            costs.push_back({cust, &route, i, saving});
        }
    }

    // Sort by saving (highest saving = worst customer)
    std::sort(costs.begin(), costs.end(), [](const auto& a, const auto& b) {
        return a.saving > b.saving;
    });

    int toRemove = static_cast<int>(solution.getAllCustomers().size() * rate);
    if (toRemove == 0 && !costs.empty()) toRemove = 1;

    // Remove worst customers, being careful about changing route indices
    for (int i = 0; i < toRemove && !costs.empty(); ++i) {
        // Find the customer to remove from the original list (which is sorted)
        auto customer_to_remove = costs[0].customer;
        removed_customers.push_back(customer_to_remove);

        // Remove from the solution
        for (auto& route : solution.routes) {
            if (route.removeCustomer(customer_to_remove)) break;
        }

        // Re-calculate costs or remove affected customers from the list
        // Easiest way is to just remove all entries from the cost list that were in the affected route
        costs.erase(std::remove_if(costs.begin(), costs.end(), 
            [&](const CustomerCost& cost) {
                return cost.customer == customer_to_remove || cost.route == costs[0].route;
            }), costs.end());
    }

    return removed_customers;
}

// --- ShawRemoval ---
double ShawRemoval::relatedness(const Customer* c1, const Customer* c2, const Instance& instance) const {
    // auto inst = ServiceLocator::getService<Instance>(); // Removed

    // 1. Distance similarity
    double dist = instance.getDistance(c1->getId(), c2->getId());
    double maxDist = 500.0; // Tune this
    
    // 2. Time window similarity  
    double timeDiff = std::abs(c1->getReadyTime() - c2->getReadyTime());
    double maxTime = 100.0; // Tune this
    
    // 3. Demand similarity
    double demandDiff = std::abs(c1->getDemand() - c2->getDemand());
    double maxDemand = 50.0; // Tune this
    
    // Weighted score (lower = more similar)
    return 0.5 * (dist/maxDist) + 0.3 * (timeDiff/maxTime) + 0.2 * (demandDiff/maxDemand);
}

std::vector<std::shared_ptr<Customer>> ShawRemoval::destroy(Solution& solution, const Instance& instance, double rate, std::mt19937& rng) {
    std::vector<std::shared_ptr<Customer>> removed_customers;
    auto all_customers = solution.getAllCustomers();
    if (all_customers.empty()) {
        return removed_customers;
    }

    int toRemove = static_cast<int>(all_customers.size() * rate);
    if (toRemove == 0 && !all_customers.empty()) {
        toRemove = 1;
    }

    // 1. Pick random seed customer
    auto seed = solution.getRandomCustomer(rng);
    if (!seed) return removed_customers;

    // Add seed to removed list and remove from solution
    removed_customers.push_back(seed);
    for (auto& route : solution.routes) {
        if (route.removeCustomer(seed)) break;
    }

    // 2. Find most related customers
    std::vector<std::pair<std::shared_ptr<Customer>, double>> candidates;
    all_customers = solution.getAllCustomers(); // Get updated list

    for (auto& c : all_customers) {
        candidates.push_back({c, relatedness(seed.get(), c.get(), instance)});
    }
    
    // Sort by relatedness (lower is more related)
    std::sort(candidates.begin(), candidates.end(), 
        [](const auto& a, const auto& b) { return a.second < b.second; });
    
    // 3. Remove top-k most related
    for (int i = 0; i < toRemove - 1 && i < candidates.size(); ++i) {
        auto customer_to_remove = candidates[i].first;
        removed_customers.push_back(customer_to_remove);
        for (auto& route : solution.routes) {
            if (route.removeCustomer(customer_to_remove)) break;
        }
    }
    
    return removed_customers;
}

// --- ZoneRemoval ---
std::vector<std::shared_ptr<Customer>> ZoneRemoval::destroy(Solution& solution, const Instance& instance, double rate, std::mt19937& rng) {
    std::vector<std::shared_ptr<Customer>> removed_customers;
    auto all_customers = solution.getAllCustomers();
    if (all_customers.empty()) {
        return removed_customers;
    }

    // auto instance = ServiceLocator::getService<Instance>(); // Removed

    // 1. Pick random center
    auto center = solution.getRandomCustomer(rng);
    if (!center) return removed_customers;

    // 2. Find customers nearby
    std::vector<std::pair<std::shared_ptr<Customer>, double>> nearby;
    for (const auto& c : all_customers) {
        if (c == center) continue;
        double dist = instance.getDistance(center->getId(), c->getId());
        nearby.push_back({c, dist});
    }

    // Sort by distance
    std::sort(nearby.begin(), nearby.end(),
        [](const auto& a, const auto& b) { return a.second < b.second; });

    int toRemove = static_cast<int>(all_customers.size() * rate);
    if (toRemove == 0 && !all_customers.empty()) toRemove = 1;

    // Remove center customer
    removed_customers.push_back(center);
    for (auto& route : solution.routes) {
        if (route.removeCustomer(center)) break;
    }

    // Remove closest ones
    for (int i = 0; i < toRemove - 1 && i < nearby.size(); ++i) {
        auto customer_to_remove = nearby[i].first;
        removed_customers.push_back(customer_to_remove);
        for (auto& route : solution.routes) {
            if (route.removeCustomer(customer_to_remove)) break;
        }
    }

    return removed_customers;
}