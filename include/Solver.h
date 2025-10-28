#ifndef SOLVER_H
#define SOLVER_H

#include <vector>
#include <random>      // Added for mt19937
#include "Instance.h"
#include "Route.h"
#include "Customer.h"   // Added for Customer

// Represents a complete solution for the EVRP, consisting of multiple routes
// and the corresponding objective values.
struct Solution {
    std::vector<Route> routes;
    std::vector<double> objectives;

    void setObjectiveValues(const std::vector<double>& values) {
        objectives = values;
    }

    // Helper to get all customers, needed for some destroy operators
    std::vector<std::shared_ptr<Customer>> getAllCustomers() const {
        std::vector<std::shared_ptr<Customer>> customers;
        for (const auto& route : routes) {
            for (const auto& info : route.getInfos()) {
                if (auto customer = std::dynamic_pointer_cast<Customer>(info.node)) {
                    customers.push_back(customer);
                }
            }
        }
        return customers;
    }

    std::shared_ptr<Customer> getRandomCustomer(std::mt19937& rng) const {
        auto customers = getAllCustomers();
        if (customers.empty()) {
            return nullptr;
        }
        std::uniform_int_distribution<> dist(0, customers.size() - 1);
        return customers[dist(rng)];
    }

    std::string toString() const;
};

// Abstract base class for all solver implementations.
class Solver {
public:
    virtual std::vector<Solution> solve() = 0;
    virtual ~Solver() = default;

protected:
    Solver() = default;
};

#endif // SOLVER_H
