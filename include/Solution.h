#pragma once
#include <vector>
#include <memory>
#include <set>
#include "Route.h"

class Solution {
public:
    explicit Solution(const std::shared_ptr<Instance>& instance);

    // --- Modifications ---
    void addRoute(const Route& route);
    void removeRoute(size_t index);
    void clear();

    // --- Getters ---
    const std::vector<Route>& getRoutes() const;
    std::vector<Route>& getRoutes(); // dùng để có thể thay đổi 1 route nào đó theo ý muốn
    double getTotalDistance() const;
    double getTotalEnergy() const;
    double getTotalTime() const;
    int getTotalVehicles()const;
    bool isFeasible() const;

    // --- Utilities ---
    size_t getNumRoutes() const { return routes.size(); }
    void evaluateRoutes();
    void evaluate();

private:
    std::shared_ptr<Instance> instance;
    std::vector<Route> routes;

    bool checkGlobalFeasibility() const;
};
