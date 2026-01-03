#include "../../include/core/Instance.h"
#include "core/Customer.h"
#include "core/Station.h"
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

    this->maxTimeWindow = 0;
    this->maxDistance = 0;
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

            if (maxDistance < distance) maxDistance = distance;
            if (maxTimeWindow < time) maxTimeWindow = time;
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

int Instance::getNearestStationId(int nodeId) const {
    // 1. KHỞI TẠO CACHE (LAZY INITIALIZATION)
    // Nếu cache chưa được tạo, hãy tạo nó ngay lần gọi đầu tiên
    if (nearestStationCache.empty()) {
        // Tìm ID lớn nhất để khởi tạo kích thước vector an toàn
        int maxId = 0;
        if (!nodes.empty()) {
            maxId = nodes.back()->getId(); // Giả định nodes được sort, hoặc duyệt tìm max
            for(const auto& n : nodes) {
                if(n->getId() > maxId) maxId = n->getId();
            }
        }

        // Khởi tạo vector với giá trị -1 (nghĩa là chưa tính)
        // Kích thước là maxId + 1 để truy cập trực tiếp bằng nodeId
        nearestStationCache.assign(maxId + 1, -1);
    }

    // 2. KIỂM TRA NODE ID HỢP LỆ
    if (nodeId < 0 || nodeId >= nearestStationCache.size()) {
        return -1; // Hoặc throw exception tùy bạn
    }

    // 3. TRẢ VỀ NẾU ĐÃ CÓ TRONG CACHE (O(1))
    if (nearestStationCache[nodeId] != -1) {
        return nearestStationCache[nodeId];
    }

    // 4. TÍNH TOÁN NẾU CHƯA CÓ (O(S) - S là số lượng trạm)
    double minDistance = std::numeric_limits<double>::max();
    int bestStationId = -1;

    // Duyệt qua tất cả các trạm sạc để tìm trạm gần nhất
    for (const auto& station : stations) { // stations là vector<shared_ptr<Station>> có sẵn trong Instance
        double dist = getDistance(nodeId, station->getId());

        if (dist < minDistance) {
            minDistance = dist;
            bestStationId = station->getId();
        }
    }

    // 5. LƯU VÀO CACHE VÀ TRẢ VỀ
    nearestStationCache[nodeId] = bestStationId;
    return bestStationId;
}

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