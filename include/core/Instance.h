#pragma once
#include <vector>
#include <memory>
#include <set>
#include <tuple>
#include <map>
#include <string>
#include "Node.h"
#include "Station.h"
#include "Customer.h"


struct DistanceMatrix {
    private:
        // OPTIMIZATION 1: Sử dụng 1D array thay vì 2D vector
        std::vector<double> distances;  // size x size, flattened
        std::vector<double> times;
        size_t size;
        double velocity;

        // OPTIMIZATION 2: Direct array access thay vì map
        // Giả định ID là contiguous từ 0 đến n-1
        inline size_t index(int from, int to) const {
            return from * size + to;
        }

    public:
        double maxDistance;
        double maxTimeWindow;

        DistanceMatrix(const std::vector<std::shared_ptr<Node>>& nodes, double vehicleVelocity);

        // OPTIMIZATION 3: Inline hot functions
        inline double getDistance(int from_id, int to_id) const {
            return distances[index(from_id, to_id)];
        }

        inline double getTime(int from_id, int to_id) const {
            return times[index(from_id, to_id)];
        }
};

class Instance {
public:
    Instance(const std::vector<std::shared_ptr<Node>>& nodes, 
             double vehicleCapacity, 
             double vehicleBattery, 
             double vehicleEnergyRate, 
             double vehicleVelocity);
    ~Instance(); // Must be defined in .cpp to handle unique_ptr to incomplete type


    const std::vector<std::shared_ptr<Customer>>&  getCustomers() const;
    const std::vector<std::shared_ptr<Station>>&  getStations() const;


    double getMaxDistance() const;
    double getMaxTimeWindow() const;
    double getMaxDemand() const;


    const std::vector<std::shared_ptr<Node>>& getNodes() const;
    double getVehicleCapacity() const;
    double getVehicleBattery() const;
    double getVehicleEnergyRate() const;
    double getVehicleVelocity() const;
    std::set<std::tuple<int>> getStationIds() const;

    const std::shared_ptr<Node>& getNodeById(int id) const;

    // New methods for cached distances
    double getDistance(int from_id, int to_id) const;
    double getTime(int from_id, int to_id) const;

    std::string toString() const;

    int getNearestStationId(int nodeId) const;

private:
    std::vector<std::shared_ptr<Node>> nodes;
    // Vehicle template parameters
    double vehicleCapacity;   // C
    double vehicleBattery;    // Q
    double vehicleEnergyRate; // r
    double vehicleVelocity;   // v

    double maxDemand;

    std::vector<std::shared_ptr<Customer>> customers;
    std::vector<std::shared_ptr<Station>> stations;

    std::set<std::tuple<int>> stationIds;
    // Distance cache
    std::unique_ptr<DistanceMatrix> distanceMatrix;

    mutable std::vector<int> nearestStationCache;
};
