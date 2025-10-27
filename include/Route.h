#pragma once
#include <vector>
#include <memory>
#include "Vehicle.h"
#include "RouteInfo.h"
#include "Instance.h"
#include "Customer.h"

// Struct to hold the result of an insertion evaluation
struct EvaluationResult {
    bool isFeasible = false;
    double costDelta = std::numeric_limits<double>::max();
};

class Route {
public:
    Route(int id, std::shared_ptr<Vehicle> vehicle, std::shared_ptr<const Instance> instance);
    ~Route() = default;

    int getId() const;
    std::shared_ptr<Vehicle> getVehicle() const;
    const std::vector<RouteInfo>& getInfos() const;

    // --- New evaluation method for ALNS ---
    EvaluationResult evaluateInsertion(std::shared_ptr<Node> node, size_t position);

    // --- Modified methods for route modification ---
    void insert(std::shared_ptr<Node> node, size_t position);
    void remove(size_t position);
    bool removeCustomer(std::shared_ptr<Customer> customer);

    // --- Getters for objectives ---
    double getTotalDistance() const;
    double getTotalTime() const;
    double getTotalEnergyCharged() const;
    int getCustomerCount() const;

    // --- Utility Getters ---
    double getCurrentLoad() const;
    double getCurrentBattery() const;

private:
    int id;
    std::shared_ptr<Vehicle> vehicle;
    std::vector<RouteInfo> infos;
    std::shared_ptr<const Instance> instance;

    // Cache results
    mutable double cachedTotalDistance;
    mutable double cachedTotalTime;
    mutable double cachedTotalEnergyCharged;
    mutable bool cacheValid;

    void invalidateCache();
    void rebuildCache() const;

    // --- Core logic for updating route state ---
    bool propogateAndUpdate(std::vector<RouteInfo>& route_infos, size_t start_index);
    void recalculateFrom(size_t start_index);
};