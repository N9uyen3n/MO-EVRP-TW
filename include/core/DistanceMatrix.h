// include/core/DistanceMatrix.h
#pragma once
#include <vector>
#include <memory>
#include <map>
#include <stdexcept>
#include "../Node.h"
#include "../Utils.h"

class DistanceMatrix {
private:
    std::vector<std::vector<double>> distances;
    std::vector<std::vector<double>> times;
    size_t size;
    double velocity;
    std::map<int, size_t> nodeId_to_index;
    
public:
    DistanceMatrix(const std::vector<std::shared_ptr<Node>>& nodes, double vehicleVelocity) 
        : size(nodes.size()), velocity(vehicleVelocity) {
        
        // Pre-allocate
        distances.resize(size, std::vector<double>(size, 0.0));
        times.resize(size, std::vector<double>(size, 0.0));
        
        // Create a map from the actual node ID to its index in the vector (0, 1, 2, ...)
        for (size_t i = 0; i < size; ++i) {
            nodeId_to_index[nodes[i]->getId()] = i;
        }

        // Precompute all distances and times using the vector indices
        for (size_t i = 0; i < size; ++i) {
            for (size_t j = 0; j < size; ++j) {
                if (i != j) {
                    double dist = Utils::euclideanDistance(
                        *nodes[i], *nodes[j]
                    );
                    distances[i][j] = dist;
                    times[i][j] = dist / velocity;
                }
            }
        }
    }
    
    double getDistance(int from_id, int to_id) const {
        auto it_from = nodeId_to_index.find(from_id);
        auto it_to = nodeId_to_index.find(to_id);

        if (it_from != nodeId_to_index.end() && it_to != nodeId_to_index.end()) {
            return distances[it_from->second][it_to->second];
        }
        throw std::runtime_error("Invalid node ID provided to getDistance: from=" + std::to_string(from_id) + ", to=" + std::to_string(to_id));
    }
    
    double getTime(int from_id, int to_id) const {
        auto it_from = nodeId_to_index.find(from_id);
        auto it_to = nodeId_to_index.find(to_id);

        if (it_from != nodeId_to_index.end() && it_to != nodeId_to_index.end()) {
            return times[it_from->second][it_to->second];
        }
        throw std::runtime_error("Invalid node ID provided to getTime: from=" + std::to_string(from_id) + ", to=" + std::to_string(to_id));
    }
};

