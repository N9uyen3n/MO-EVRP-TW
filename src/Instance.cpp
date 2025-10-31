#include "../include/Instance.h"
#include <sstream>
#include <stdexcept>
#include <cmath>
#include <string>

DistanceMatrix::DistanceMatrix(const std::vector<std::shared_ptr<Node>>& nodes, double vehicleVelocity)
    : velocity(vehicleVelocity)
{
    size = nodes.size();
    distances.resize(size, std::vector<double>(size, 0.0));
    times.resize(size, std::vector<double>(size, 0.0));

    // Ánh xạ node_id → index trong mảng
    for (size_t i = 0; i < size; ++i) {
        nodeId_to_index[nodes[i]->getId()] = i;
    }

    // Tính khoảng cách và thời gian cho mọi cặp (i, j)
    for (size_t i = 0; i < size; ++i) {
        for (size_t j = 0; j < size; ++j) {
            if (i == j) continue;

            double dx = nodes[i]->getX() - nodes[j]->getX();
            double dy = nodes[i]->getY() - nodes[j]->getY();

            double distance = std::sqrt(dx * dx + dy * dy);
            double time = distance / velocity;

            distances[i][j] = distance;
            times[i][j] = time;
        }
    }
}

double DistanceMatrix::getDistance(int from_id, int to_id) const {
    auto itFrom = nodeId_to_index.find(from_id);
    auto itTo = nodeId_to_index.find(to_id);
    if (itFrom == nodeId_to_index.end() || itTo == nodeId_to_index.end()) {
        throw std::runtime_error("Invalid node ID in getDistance()");
    }
    return distances[itFrom->second][itTo->second];
}

double DistanceMatrix::getTime(int from_id, int to_id) const {
    auto itFrom = nodeId_to_index.find(from_id);
    auto itTo = nodeId_to_index.find(to_id);
    if (itFrom == nodeId_to_index.end() || itTo == nodeId_to_index.end()) {
        throw std::runtime_error("Invalid node ID in getTime()");
    }
    return times[itFrom->second][itTo->second];
}


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

std::set<std::tuple<int>> Instance::getStationIds() const {
    return this->stationIds;
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

std::string Instance::toString() const {
    std::stringstream ss;
    ss << "Capacity: " << vehicleCapacity << ", Battery: " << vehicleBattery << ", Energy rate: " << vehicleEnergyRate
    << "Velocity" << vehicleVelocity;
    return ss.str();
}