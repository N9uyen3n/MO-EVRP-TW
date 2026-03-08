#include "../../include/core/Instance.h"
#include "core/Customer.h"
#include "core/Station.h"
#include <sstream>
#include <stdexcept>
#include <cmath>
#include <string>

DistanceMatrix::DistanceMatrix(const std::vector<std::shared_ptr<Node>>& nodes,
                               double vehicleVelocity)
    : velocity(vehicleVelocity), size(nodes.size())
{
    // OPTIMIZATION 4: Single allocation
    distances.resize(size * size, 0.0);
    times.resize(size * size, 0.0);

    maxTimeWindow = 0;
    maxDistance = 0;

    // OPTIMIZATION 5: Parallel calculation (if available)
    // #pragma omp parallel for if(size > 100)
    for (size_t i = 0; i < size; ++i) {
        const double xi = nodes[i]->getX();
        const double yi = nodes[i]->getY();

        for (size_t j = i + 1; j < size; ++j) {
            const double dx = xi - nodes[j]->getX();
            const double dy = yi - nodes[j]->getY();

            double distance = std::sqrt(dx * dx + dy * dy);
            double time = distance / velocity;

            // Symmetric matrix
            size_t idx_ij = index(i, j);
            size_t idx_ji = index(j, i);

            distances[idx_ij] = distances[idx_ji] = distance;
            times[idx_ij] = times[idx_ji] = time;

            // OPTIMIZATION 6: Local max tracking
            if (distance > maxDistance) maxDistance = distance;
            if (time > maxTimeWindow) maxTimeWindow = time;
        }
    }
}

// Instance.cpp - Optimized nearest station
int Instance::getNearestStationId(int nodeId) const {
    // OPTIMIZATION 7: Build cache on first use, not in constructor
    if (nearestStationCache.empty()) {
        int maxId = nodes.back()->getId();
        nearestStationCache.assign(maxId + 1, -1);
    }

    if (nodeId < 0 || nodeId >= nearestStationCache.size()) {
        return -1;
    }

    // Return cached value
    if (nearestStationCache[nodeId] != -1) {
        return nearestStationCache[nodeId];
    }

    // OPTIMIZATION 8: Early termination with distance threshold
    double minDistance = std::numeric_limits<double>::max();
    int bestStationId = -1;

    // Use direct iteration instead of shared_ptr dereferencing
    for (const auto& station : stations) {
        double dist = getDistance(nodeId, station->getId());

        if (dist < minDistance) {
            minDistance = dist;
            bestStationId = station->getId();

            // Early exit if very close
            if (dist < 1.0) break;
        }
    }

    nearestStationCache[nodeId] = bestStationId;
    return bestStationId;
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

    this-> maxDemand = 0;
    for (auto& node : nodes) {
        // Use dynamic_cast to check if a Node is actually a Customer
        if (auto customer = std::dynamic_pointer_cast<Customer>(node)) {
            if (maxDemand < customer->getDemand()) maxDemand = customer->getDemand();
            customers.push_back(customer);
        }

        if (auto station = std::dynamic_pointer_cast<Station>(node)) {
            stations.push_back(station);
        }
    }
}

// Destructor must be defined here where DistanceMatrix is a complete type
Instance::~Instance() = default;

const std::vector<std::shared_ptr<Node>>& Instance::getNodes() const {
    return nodes;
}

const std::vector<std::shared_ptr<Customer>>&  Instance::getCustomers() const {
    return customers;
}
const std::vector<std::shared_ptr<Station>>&  Instance::getStations() const {
    return stations;
}

double Instance::getMaxDistance() const {
    return this->distanceMatrix->maxDistance;
}

double Instance::getMaxTimeWindow() const {
    return this->distanceMatrix->maxTimeWindow;
}

double Instance::getMaxDemand() const {
    return this->maxDemand;
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





std::string Instance::toString() const {
    std::stringstream ss;
    ss << "Capacity: " << vehicleCapacity << ", Battery: " << vehicleBattery << ", Energy rate: " << vehicleEnergyRate
    << "Velocity" << vehicleVelocity;
    return ss.str();
}