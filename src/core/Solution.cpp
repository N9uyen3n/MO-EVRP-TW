#include "../../include/core/Solution.h"
#include "../../include/core/Instance.h"
#include "../../include/core/Customer.h"
#include <iostream>
#include <ostream>
#include <sstream>
#include <iomanip>
#include <set>
#include <numeric> // Required for std::accumulate
#include <cmath>   // Required for std::pow

// Private detach function for Copy-on-Write.
// If the data is shared (ref count > 1), it creates a deep copy and points to it,
// ensuring that modifications do not affect other Solution objects.
void Solution::detach() {
    if (routes.use_count() > 1) {
        routes = std::make_shared<std::vector<Route>>(*routes);
    }
}

Solution::Solution(const std::shared_ptr<Instance>& instance)
    : instance(instance),
      routes(std::make_shared<std::vector<Route>>()), // Initialize with a new vector
      totalVehicles(0),
      totalDistance(0.0),
      workloadVariance(0.0),
      totalTime(0.0),
      maxTime(0.0),
      feasible(true)
{}

// Copy constructor: Just copy the shared_ptr. This is a fast, shallow copy.
Solution::Solution(const Solution& other) = default;

// Copy assignment operator: Default behavior is correct for shared_ptr.
Solution& Solution::operator=(const Solution& other) = default;


void Solution::addRoute(const Route& route) {
    detach(); // Ensure unique copy before modification
    routes->push_back(route);
}

void Solution::removeRoute(size_t index) {
    detach(); // Ensure unique copy before modification
    if (index >= routes->size()) {
        throw std::out_of_range("Chỉ số tuyến đường không hợp lệ.");
    }
    routes->erase(routes->begin() + index);
}

void Solution::clear() {
    detach(); // Ensure unique copy before modification
    routes->clear();
}

const std::vector<Route>& Solution::getRoutes() const {
    return *routes; // Return const reference to the vector
}

std::vector<Route>& Solution::getRoutes() {
    detach(); // Ensure unique copy before returning a mutable reference
    return *routes;
}

size_t Solution::getNumRoutes() const {
    return routes->size(); // Use pointer
}

double Solution::getTotalDistance() const {
    return this->totalDistance;
}

double Solution::getWorkloadVariance() const {
    return this->workloadVariance;
}

double Solution::getTotalTime() const {
    double total = 0.0;
    for (const auto& route : *routes) { // Use pointer
        total += route.getTotalTime();
    }
    return total;
}

double Solution::getMaxTime() const {
    double total = 0.0;
    for (const auto& route : *routes) { // Use pointer
        if (total < route.getTotalTime()) total = route.getTotalTime();
    }
    return total;
}

int Solution::getTotalVehicles() const {
    return this->totalVehicles;
}

bool Solution::isFeasible() const {
    return checkGlobalFeasibility();
}

void Solution::evaluateRoutes() {
    detach(); // We are about to modify the routes vector itself (erase)

    for (auto& route : *this->routes) { // Use pointer
        route.evaluate();
    }

    routes->erase(
        std::remove_if(routes->begin(), routes->end(),
            [](const Route& route) {
                return route.getNodes().size() <= 2;
            }
        ),
        routes->end()
    );

    this->totalVehicles = routes->size(); // Use pointer
    this->totalDistance = 0.0;
    this->workloadVariance = 0.0;
    this->totalTime = 0.0;
    this->maxTime = 0.0;
    this->feasible = true;

    std::vector<double> route_durations;
    route_durations.reserve(routes->size());

    for (const auto& route : *this->routes) { // Use pointer
        this->totalDistance += route.getTotalDistance();
        this->totalTime += route.getTotalTime();
        route_durations.push_back(route.getTotalTime());

        if (route.getTotalTime() > this->maxTime) {
            this->maxTime = route.getTotalTime();
        }

        if (!route.isFeasible()) {
            this->feasible = false;
        }
    }

    // Calculate Workload Variance
    if (route_durations.size() > 1) {
        double sum = std::accumulate(route_durations.begin(), route_durations.end(), 0.0);
        double mean = sum / route_durations.size();
        double sq_sum = 0.0;
        for(const auto& d : route_durations) {
            sq_sum += std::pow(d - mean, 2);
        }
        this->workloadVariance = sq_sum / route_durations.size();
    } else {
        this->workloadVariance = 0.0; // Variance is 0 if there's 0 or 1 route
    }
}

bool Solution::checkGlobalFeasibility() const {
    std::set<int> all_customers_in_instance;
    for (const auto& node : instance->getNodes()) {
        if (node->getType() == NodeType::CUSTOMER) {
            all_customers_in_instance.insert(node->getId());
        }
    }

    std::set<int> customers_served;

    for (const Route& route : *this->routes) { // Use pointer
        if (!route.isFeasible()) {
            return false;
        }

        for (int node_id : route.getNodes()) {
            if (all_customers_in_instance.count(node_id)) {
                if (customers_served.count(node_id)) {
                    return false;
                }
                customers_served.insert(node_id);
            }
        }
    }

    return customers_served.size() == all_customers_in_instance.size();
}

bool Solution::dominates(const Solution& other) const {
    // Ưu tiên 1 (Tuyệt đối): Số lượng xe (totalVehicles).
    if (this->totalVehicles < other.totalVehicles) {
        return true; // Thắng ngay lập tức nếu ít xe hơn.
    }
    if (this->totalVehicles > other.totalVehicles) {
        return false; // Thua ngay lập tức nếu nhiều xe hơn.
    }

    // Ưu tiên 2: Nếu số xe bằng nhau, xét Pareto trên các mục tiêu còn lại.
    // A dominates B <=> A không tệ hơn B ở mọi mặt VÀ A tốt hơn B ở ít nhất 1 mặt.
    bool betterInAtLeastOne = false;

    // CHECK 2: Distance
    if (this->totalDistance > other.totalDistance + 1e-4) return false; // Tệ hơn
    if (this->totalDistance < other.totalDistance - 1e-4) betterInAtLeastOne = true;

    // CHECK 3: Workload Variance
    if (this->workloadVariance > other.workloadVariance + 1e-4) return false; // Tệ hơn
    if (this->workloadVariance < other.workloadVariance - 1e-4) betterInAtLeastOne = true;

    // CHECK 4: Max Time
    if (this->maxTime > other.maxTime + 1e-4) return false; // Tệ hơn
    if (this->maxTime < other.maxTime - 1e-4) betterInAtLeastOne = true;

    return betterInAtLeastOne;
}

/*
// PHIÊN BẢN THAY THẾ: So sánh Pareto ngang hàng trên tất cả các mục tiêu
bool Solution::dominates_flat_pareto(const Solution& other) const {
    // A dominates B <=> A không tệ hơn B ở mọi mặt VÀ A tốt hơn B ở ít nhất 1 mặt.
    bool betterInAtLeastOne = false;

    // CHECK 1: Total Vehicles
    if (this->totalVehicles > other.totalVehicles) return false; // Tệ hơn
    if (this->totalVehicles < other.totalVehicles) betterInAtLeastOne = true;

    // CHECK 2: Distance
    if (this->totalDistance > other.totalDistance + 1e-4) return false; // Tệ hơn
    if (this->totalDistance < other.totalDistance - 1e-4) betterInAtLeastOne = true;

    // CHECK 3: Workload Variance
    if (this->workloadVariance > other.workloadVariance + 1e-4) return false; // Tệ hơn
    if (this->workloadVariance < other.workloadVariance - 1e-4) betterInAtLeastOne = true;

    // CHECK 4: Max Time
    if (this->maxTime > other.maxTime + 1e-4) return false; // Tệ hơn
    if (this->maxTime < other.maxTime - 1e-4) betterInAtLeastOne = true;

    return betterInAtLeastOne;
}
*/

void Solution::print() const {
    std::cout << "Total Vehicles: " << getTotalVehicles() << " Total Distance: " << getTotalDistance()
    << " Workload Var: " << getWorkloadVariance() << " Total Time: " << getTotalTime() << " Max Time: " << getMaxTime() << std::endl;
    for (const auto& route : *routes) { // Use pointer
        route.print();
    }
}

std::string Solution::toString() const {
    std::stringstream ss;
    ss << std::fixed << std::setprecision(2);

    ss << "Solution (Vehicles: " << getTotalVehicles()
       << ", Distance: " << getTotalDistance()
       << ", WorkloadVar: " << getWorkloadVariance()
       << ", MaxTime: " << getMaxTime()
       << ", Feasible: " << (isFeasible() ? "true" : "false") << ")\n";

    for (const auto& route : *routes) { // Use pointer
        ss << route.toString();
    }

    return ss.str();
}

long long Solution::getHash() const {
    long long hash = 0;
    for (const auto& route : *routes) { // Use pointer
        hash ^= route.getHash();
    }
    return hash;
}
