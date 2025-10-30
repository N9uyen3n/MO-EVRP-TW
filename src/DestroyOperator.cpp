// SỬA: Thêm include này ở dòng ĐẦU TIÊN
#include "Operators.h"

#include "Customer.h"
#include "Station.h"
#include <set>
#include <algorithm> // cho std::sort, std::shuffle
#include <cmath>     // cho std::floor, std::pow
#include <vector>
#include <map>

// Cấu trúc helper để sắp xếp
struct NodeCost {
    int routeIdx;
    size_t nodePos;
    double cost;
    bool operator>(const NodeCost& other) const { return cost > other.cost; }
};

struct RelatedNode {
    int custId;
    double relatedness;
    bool operator<(const RelatedNode& other) const { return relatedness < other.relatedness; }
};

struct RouteCustomerCount {
    int routeIdx;
    int customerCount;
    bool operator<(const RouteCustomerCount& other) const {
        return customerCount < other.customerCount;
    }
};

// ==================================================================
// PHÁ HỦY KHÁCH HÀNG (CUSTOMER DESTROY)
// ==================================================================

ICustomerDestroy::ICustomerDestroy(std::shared_ptr<Instance> instance, std::mt19937& rng)
    : instance(instance), rng(rng) {}

// SỬA: Thêm định nghĩa cho hàm helper
std::set<int> ICustomerDestroy::getAllCustomerIds() const {
    std::set<int> customerIds;
    for (const auto& node : instance->getNodes()) {
        if (std::dynamic_pointer_cast<Customer>(node)) {
            customerIds.insert(node->getId());
        }
    }
    return customerIds;
}

// --- RandomRemoval ---
void RandomRemoval::destroy(Solution& solution, int numToRemove, std::vector<int>& unservedCustomers) {
    // SỬA: Gọi hàm helper từ lớp cơ sở
    auto allCustomers = this->getAllCustomerIds();
    std::vector<int> allCustomersVec(allCustomers.begin(), allCustomers.end());
    if (allCustomersVec.empty()) return;

    std::shuffle(allCustomersVec.begin(), allCustomersVec.end(), rng);
    int actualToRemove = std::min(numToRemove, static_cast<int>(allCustomersVec.size()));

    std::set<int> toRemoveSet;
    for(int i = 0; i < actualToRemove; ++i) {
        toRemoveSet.insert(allCustomersVec[i]);
        unservedCustomers.push_back(allCustomersVec[i]);
    }

    for (auto& route : solution.getRoutes()) {
        bool changed = false;
        std::vector<int> newNodeSeq;
        for (int nodeId : route.getNodes()) {
            if (toRemoveSet.count(nodeId)) {
                changed = true;
            } else {
                newNodeSeq.push_back(nodeId);
            }
        }
        if (changed) {
            route.clear();
            for (size_t i = 1; i < newNodeSeq.size() - 1; ++i) {
                route.addNode(newNodeSeq[i], i);
            }
        }
    }
}

// --- ShawRemoval ---
ShawRemoval::ShawRemoval(std::shared_ptr<Instance> inst, std::mt19937& r,
                         double p1, double p2, double p3, double p4, double eta)
    : ICustomerDestroy(inst, r), phi1(p1), phi2(p2), phi3(p3), phi4(p4), determinism(eta) {}

double ShawRemoval::calculateRelatedness(int custId1, int custId2, const Solution& sol) {
    auto node1 = std::dynamic_pointer_cast<Customer>(instance->getNodeById(custId1));
    auto node2 = std::dynamic_pointer_cast<Customer>(instance->getNodeById(custId2));
    if (!node1 || !node2) return std::numeric_limits<double>::infinity();

    double d_ij = instance->getDistance(custId1, custId2);
    double time_diff = std::abs(node1->getReadyTime() - node2->getReadyTime());
    double demand_diff = std::abs(node1->getDemand() - node2->getDemand());

    double l_ij = 1.0;
    for(const auto& route : sol.getRoutes()) {
        bool found1 = false, found2 = false;
        for (int id : route.getNodes()) {
            if (id == custId1) found1 = true;
            if (id == custId2) found2 = true;
        }
        if (found1 && found2) {
            l_ij = -1.0;
            break;
        }
    }
    return phi1 * d_ij + phi2 * time_diff + phi3 * l_ij + phi4 * demand_diff;
}

void ShawRemoval::destroy(Solution& solution, int numToRemove, std::vector<int>& unservedCustomers) {
    auto allCustomers = this->getAllCustomerIds();
    std::vector<int> servedCustomers(allCustomers.begin(), allCustomers.end());
    if (servedCustomers.empty()) return;

    std::set<int> removedSet;
    std::uniform_int_distribution<size_t> dist(0, servedCustomers.size() - 1);
    int firstCustId = servedCustomers[dist(rng)];

    unservedCustomers.push_back(firstCustId);
    removedSet.insert(firstCustId);

    for (int i = 1; i < numToRemove && servedCustomers.size() > removedSet.size(); ++i) {
        std::uniform_int_distribution<size_t> removedDist(0, unservedCustomers.size() - 1);
        int j_custId = unservedCustomers[removedDist(rng)];

        std::vector<RelatedNode> relatednessList;
        for (int k_custId : servedCustomers) {
            if (removedSet.count(k_custId) == 0) {
                double r = calculateRelatedness(j_custId, k_custId, solution);
                relatednessList.push_back({k_custId, r});
            }
        }

        if (relatednessList.empty()) break;
        std::sort(relatednessList.begin(), relatednessList.end());

        std::uniform_real_distribution<double> rand_dist(0.0, 1.0);
        double y = rand_dist(rng);
        int index = static_cast<int>(std::floor(std::pow(y, determinism) * relatednessList.size()));

        int chosenCustId = relatednessList[index].custId;
        unservedCustomers.push_back(chosenCustId);
        removedSet.insert(chosenCustId);
    }

    for (auto& route : solution.getRoutes()) {
        bool changed = false;
        std::vector<int> newNodeSeq;
        for (int nodeId : route.getNodes()) {
            if (removedSet.count(nodeId)) changed = true;
            else newNodeSeq.push_back(nodeId);
        }
        if (changed) {
            route.clear();
            for (size_t i = 1; i < newNodeSeq.size() - 1; ++i) {
                route.addNode(newNodeSeq[i], i);
            }
        }
    }
}

// --- WorstDistanceRemoval ---
WorstDistanceRemoval::WorstDistanceRemoval(std::shared_ptr<Instance> inst, std::mt19937& r, double kappa)
    : ICustomerDestroy(inst, r), determinism_kappa(kappa) {}

void WorstDistanceRemoval::destroy(Solution& solution, int numToRemove, std::vector<int>& unservedCustomers) {
    std::vector<NodeCost> customerCosts;

    for (size_t i = 0; i < solution.getRoutes().size(); ++i) {
        const auto& nodes = solution.getRoutes()[i].getNodes();
        for (size_t j = 1; j < nodes.size() - 1; ++j) {
            if (!std::dynamic_pointer_cast<Customer>(instance->getNodeById(nodes[j]))) continue;

            int prevNode = nodes[j - 1];
            int currNode = nodes[j];
            int nextNode = nodes[j + 1];
            double cost = instance->getDistance(prevNode, currNode) + instance->getDistance(currNode, nextNode);
            customerCosts.push_back({static_cast<int>(i), j, cost});
        }
    }

    if (customerCosts.empty()) return;
    std::sort(customerCosts.begin(), customerCosts.end(), std::greater<NodeCost>());

    std::set<int> removedSet;
    for (int i = 0; i < numToRemove; ++i) {
        if (customerCosts.empty()) break;

        std::uniform_real_distribution<double> rand_dist(0.0, 1.0);
        double y = rand_dist(rng);
        int index = static_cast<int>(std::floor(std::pow(y, determinism_kappa) * customerCosts.size()));

        NodeCost toRemove = customerCosts[index];
        customerCosts.erase(customerCosts.begin() + index);

        int custId = solution.getRoutes()[toRemove.routeIdx].getNodes()[toRemove.nodePos];
        if (removedSet.count(custId) == 0) {
            unservedCustomers.push_back(custId);
            removedSet.insert(custId);
        }
    }

    for (auto& route : solution.getRoutes()) {
        bool changed = false;
        std::vector<int> newNodeSeq;
        for (int nodeId : route.getNodes()) {
            if (removedSet.count(nodeId)) changed = true;
            else newNodeSeq.push_back(nodeId);
        }
        if (changed) {
            route.clear();
            for (size_t i = 1; i < newNodeSeq.size() - 1; ++i) {
                route.addNode(newNodeSeq[i], i);
            }
        }
    }
}

// --- RouteRemoval ---
void GreedyRouteRemoval::removeCustomersFromRoute(Solution& solution, size_t routeIndex, std::vector<int>& unservedCustomers) {
    auto& route = solution.getRoutes()[routeIndex];
    for (int nodeId : route.getNodes()) {
        if (std::dynamic_pointer_cast<Customer>(instance->getNodeById(nodeId))) {
            unservedCustomers.push_back(nodeId);
        }
    }
    route.clear();
}

void GreedyRouteRemoval::destroy(Solution& solution, int numToRemove, std::vector<int>& unservedCustomers) {
    std::vector<RouteCustomerCount> routeCounts;
    for (size_t i = 0; i < solution.getRoutes().size(); ++i) {
        int count = 0;
        for (int nodeId : solution.getRoutes()[i].getNodes()) {
            if (std::dynamic_pointer_cast<Customer>(instance->getNodeById(nodeId))) {
                count++;
            }
        }
        if (count > 0) {
            routeCounts.push_back({static_cast<int>(i), count});
        }
    }

    if (routeCounts.empty()) return;
    std::sort(routeCounts.begin(), routeCounts.end());

    int actualToRemove = std::min(numToRemove, static_cast<int>(routeCounts.size()));
    for (int i = 0; i < actualToRemove; ++i) {
        removeCustomersFromRoute(solution, routeCounts[i].routeIdx, unservedCustomers);
    }
}

void RandomRouteRemoval::destroy(Solution& solution, int numToRemove, std::vector<int>& unservedCustomers) {
    std::vector<int> routeIndices;
    for(size_t i = 0; i < solution.getRoutes().size(); ++i) routeIndices.push_back(i);

    if (routeIndices.empty()) return;
    std::shuffle(routeIndices.begin(), routeIndices.end(), rng);

    int actualToRemove = std::min(numToRemove, static_cast<int>(routeIndices.size()));
    for (int i = 0; i < actualToRemove; ++i) {
        removeCustomersFromRoute(solution, routeIndices[i], unservedCustomers);
    }
}

// ==================================================================
// PHÁ HỦY TRẠM SẠC (STATION DESTROY)
// ==================================================================

IStationDestroy::IStationDestroy(std::shared_ptr<Instance> instance, std::mt19937& rng)
    : instance(instance), rng(rng) {}

void RandomStationRemoval::destroy(Solution& solution, int numToRemove) {
    std::vector<std::pair<int, size_t>> stationPositions;
    for (size_t i = 0; i < solution.getRoutes().size(); ++i) {
        const auto& nodes = solution.getRoutes()[i].getNodes();
        for (size_t j = 1; j < nodes.size() - 1; ++j) {
            if (std::dynamic_pointer_cast<Station>(instance->getNodeById(nodes[j]))) {
                stationPositions.push_back({static_cast<int>(i), j});
            }
        }
    }

    if (stationPositions.empty()) return;
    std::shuffle(stationPositions.begin(), stationPositions.end(), rng);

    int actualToRemove = std::min(numToRemove, static_cast<int>(stationPositions.size()));

    // Key: route.getId(), Value: set of nodeIds to remove
    std::map<int, std::set<int>> removedStations;

    for (int i = 0; i < actualToRemove; ++i) {
        int routeIdx = stationPositions[i].first;
        size_t pos = stationPositions[i].second;
        // SỬA: Dùng getId()
        int routeId = solution.getRoutes()[routeIdx].getId();
        int nodeId = solution.getRoutes()[routeIdx].getNodes()[pos];
        removedStations[routeId].insert(nodeId);
    }

    for (auto& route : solution.getRoutes()) {
        if (removedStations.count(route.getId()) == 0) continue; // Không có gì để xóa ở tuyến này

        bool changed = false;
        std::vector<int> newNodeSeq;
        const auto& stationsInThisRoute = removedStations.at(route.getId());

        for (int nodeId : route.getNodes()) {
            if (stationsInThisRoute.count(nodeId)) {
                changed = true;
            } else {
                newNodeSeq.push_back(nodeId);
            }
        }
        if (changed) {
            route.clear();
            for (size_t i = 1; i < newNodeSeq.size() - 1; ++i) {
                route.addNode(newNodeSeq[i], i);
            }
        }
    }
}