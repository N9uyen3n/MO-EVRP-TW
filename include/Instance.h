#pragma once
#include <vector>
#include <memory>
#include "Node.h"

// Forward declaration
class DistanceMatrix;

class Instance {
public:
    Instance(const std::vector<std::shared_ptr<Node>>& nodes, 
             double vehicleCapacity, 
             double vehicleBattery, 
             double vehicleEnergyRate, 
             double vehicleVelocity);
    ~Instance(); // Must be defined in .cpp to handle unique_ptr to incomplete type

    const std::vector<std::shared_ptr<Node>>& getNodes() const;
    double getVehicleCapacity() const;
    double getVehicleBattery() const;
    double getVehicleEnergyRate() const;
    double getVehicleVelocity() const;

    const std::shared_ptr<Node>& getNodeById(int id) const;

    // New methods for cached distances
    double getDistance(int from_id, int to_id) const;
    double getTime(int from_id, int to_id) const;

private:
    std::vector<std::shared_ptr<Node>> nodes;
    // Vehicle template parameters
    double vehicleCapacity;   // C
    double vehicleBattery;    // Q
    double vehicleEnergyRate; // r
    double vehicleVelocity;   // v

    // Distance cache
    std::unique_ptr<DistanceMatrix> distanceMatrix;
};
