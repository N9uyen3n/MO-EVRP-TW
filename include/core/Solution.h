#pragma once
#include <vector>
#include <memory>
#include <set>
#include "Route.h"

class Solution {
public:
    explicit Solution(const std::shared_ptr<Instance>& instance);
    Solution(const Solution& other); // Copy constructor
    Solution& operator=(const Solution& other); // Copy assignment

    // --- Modifications ---
    void addRoute(const Route& route);
    void removeRoute(size_t index);
    void clear();

    // --- Getters ---
    const std::vector<Route>& getRoutes() const;
    std::vector<Route>& getRoutes();
    double getTotalDistance() const;
    double getWorkloadVariance() const;
    double getGiniCoefficient() const; // ⭐ NEW: Driver Equity Metric
    double getTotalTime() const;
    double getMaxTime() const;
    int getTotalVehicles()const;
    bool isFeasible() const;

    // --- Utilities ---
    size_t getNumRoutes() const;
    void evaluateRoutes();
    double getAverageRouteTime() const; // ⭐ NEW: For workload-conscious insertion

    bool dominates(const Solution& other) const;
    void print() const;
    std::string toString() const;

    long long getHash() const;

private:
    void detach(); // Private method for Copy-on-Write
    std::shared_ptr<Instance> instance;
    std::shared_ptr<std::vector<Route>> routes; // Changed to shared_ptr

    int totalVehicles;
    double totalDistance;
    double workloadVariance;
    double giniCoefficient; // ⭐ NEW
    double totalTime;
    double maxTime;

    bool feasible;

    bool checkGlobalFeasibility() const;
};