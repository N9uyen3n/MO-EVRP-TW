#pragma once
#include <vector>
#include <memory>
#include "Vehicle.h"
#include "RouteInfo.h"
#include "Instance.h"
#include "Customer.h" // Added for removeCustomer

class Route {
public:
    Route(int id, std::shared_ptr<Vehicle> vehicle, std::shared_ptr<const Instance> instance);
    ~Route() = default;

    int getId() const;
    std::shared_ptr<Vehicle> getVehicle() const;
    const std::vector<RouteInfo>& getInfos() const;

    // Methods to modify the route
    bool canInsert(std::shared_ptr<Node> node, size_t position);
    void insert(std::shared_ptr<Node> node, size_t position);
    void remove(size_t position);

    // --- New getters for objectives ---
    double getTotalDistance() const;
    double getTotalTime() const;
    double getTotalEnergyCharged() const;
    int getCustomerCount() const;

    // --- Methods for validation ---
    double getCurrentLoad() const;
    double getCurrentBattery() const;
    double calculateArrivalTime(std::shared_ptr<Node> node, int position) const;
    double calculateEnergyToNode(std::shared_ptr<Node> node, int position) const;

    // --- Methods for ALNS operators ---
    double getInsertionCost(std::shared_ptr<Node> node, int position) const;
    bool removeCustomer(std::shared_ptr<Customer> customer);

private:
    int id;
    std::shared_ptr<Vehicle> vehicle;
    std::vector<RouteInfo> infos;
    std::shared_ptr<const Instance> instance; // Added

    // Cache results
    mutable double cachedTotalDistance;
    mutable double cachedTotalTime;
    mutable double cachedTotalEnergyCharged;
    mutable bool cacheValid;

    void invalidateCache();
    void rebuildCache() const;

    // Private helper to recalculate all states from a given position
    void recalculateFrom(size_t position);

    bool checkAndUpdateInfos(std::vector<RouteInfo>& temp_infos) const;
};
