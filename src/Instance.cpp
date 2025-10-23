#include "../include/Instance.h"
#include "../include/core/DistanceMatrix.h"
#include <stdexcept>
#include <stdexcept>   
#include <string>    


Instance::Instance(const std::vector<std::shared_ptr<Node>>& nodes, 
                 double vehicleCapacity, 
                 double vehicleBattery, 
                 double vehicleEnergyRate, 
                 double vehicleVelocity)
    : nodes(nodes),
      vehicleCapacity(vehicleCapacity),
      vehicleBattery(vehicleBattery),
      vehicleEnergyRate(vehicleEnergyRate),
      vehicleVelocity(vehicleVelocity) 
{
    // After nodes are initialized, create the distance matrix
    distanceMatrix = std::make_unique<DistanceMatrix>(nodes, vehicleVelocity);
}

// Destructor must be defined here where DistanceMatrix is a complete type
Instance::~Instance() = default;

const std::vector<std::shared_ptr<Node>>& Instance::getNodes() const {
    return nodes;
}

double Instance::getVehicleCapacity() const {
    return vehicleCapacity;
}

double Instance::getVehicleBattery() const {
    return vehicleBattery;
}

double Instance::getVehicleEnergyRate() const {
    return vehicleEnergyRate;
}

double Instance::getVehicleVelocity() const {
    return vehicleVelocity;
}

const std::shared_ptr<Node>& Instance::getNodeById(int id) const {
    if (id >= 0 && id < nodes.size()) {
        // Assuming node IDs are contiguous and 0-based, which is how they are parsed.
        return nodes[id];
    }
    throw std::runtime_error("Node with ID " + std::to_string(id) + " not found or ID is out of bounds.");
}

// --- New method implementations ---

double Instance::getDistance(int from_id, int to_id) const {
    return distanceMatrix->getDistance(from_id, to_id);
}

double Instance::getTime(int from_id, int to_id) const {
    return distanceMatrix->getTime(from_id, to_id);
}   