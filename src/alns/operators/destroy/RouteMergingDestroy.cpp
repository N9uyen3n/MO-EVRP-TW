#include "../../../../include/alns/operators/destroy/RouteMergingDestroy.h"
#include "../../../../include/core/Customer.h"
#include "../../../../include/core/Station.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

RouteMergingDestroy::RouteMergingDestroy(std::shared_ptr<Instance> instance)
    : instance(instance) {
}

std::string RouteMergingDestroy::getName() const {
    return "Route Merging Destroy";
}

std::pair<double, double>
RouteMergingDestroy::calculateRouteCentroid(const Route &route) const {
    double sumX = 0.0, sumY = 0.0;
    int customerCount = 0;

    for (int nodeId: route.getNodes()) {
        auto node = instance->getNodeById(nodeId);
        if (node->getType() == NodeType::CUSTOMER) {
            sumX += node->getX();
            sumY += node->getY();
            customerCount++;
        }
    }

    if (customerCount == 0)
        return {0.0, 0.0};
    return {sumX / customerCount, sumY / customerCount};
}

double RouteMergingDestroy::euclideanDistance(double x1, double y1, double x2,
                                              double y2) const {
    double dx = x2 - x1;
    double dy = y2 - y1;
    return std::sqrt(dx * dx + dy * dy);
}

double RouteMergingDestroy::getTotalDemand(const Route &route) const {
    double totalDemand = 0.0;
    for (int nodeId: route.getNodes()) {
        auto node = instance->getNodeById(nodeId);
        if (auto customer = std::dynamic_pointer_cast<Customer>(node)) {
            totalDemand += customer->getDemand();
        }
    }
    return totalDemand;
}

bool RouteMergingDestroy::canPotentiallyMerge(const Route &route1,
                                              const Route &route2) const {
    // Check 1: Capacity constraint (RELAXED for dense instance repacking)
    // Tolerance 1.05: Intentional relaxation to allow more merge attempts.
    // Infeasible routes will be handled and fixed by repair operators later.
    double totalDemand = getTotalDemand(route1) + getTotalDemand(route2);
    if (totalDemand > instance->getVehicleCapacity() * 1.05) {
        return false;
    }

    // Check 2: Time window constraint (rough estimate)
    // Nếu tổng thời gian của 2 tuyến quá lớn, khả năng cao không merge được
    double totalTime = route1.getTotalTime() + route2.getTotalTime();
    auto depotNode = instance->getNodeById(0);
    double maxAllowedTime = depotNode->getDueDate();

    // Heuristic: Nếu tổng thời gian > 1.5 * maxTime thì khả năng cao không merge
    // được
    if (totalTime > maxAllowedTime * 1.5) {
        return false;
    }

    return true;
}

std::vector<int> RouteMergingDestroy::execute(Solution &solution,
                                              int nodesToRemove,
                                              std::mt19937 &rng) {
    std::vector<int> removedCustomers;
    auto &routes = solution.getRoutes();

    // Estimate target number of routes to remove based on nodesToRemove
    int activeRoutes = 0;
    int totalCustomers = 0;
    for (const auto &r: routes) {
        if (r.getNodes().size() > 2) {
            activeRoutes++;
            totalCustomers += r.getCustomers().size();
        }
    }
    if (activeRoutes < 2)
        return removedCustomers;

    // Fix 1: Use customer count instead of total node count
    int avgNodesPerRoute = std::max(1, totalCustomers / activeRoutes);
    // Fix 2: Change minimum routes to remove from 2 to 1 to avoid being overly aggressive on small datasets
    int targetRoutesToRemove = std::max(1, nodesToRemove / avgNodesPerRoute);
    targetRoutesToRemove = std::min(targetRoutesToRemove, activeRoutes);

    struct Pair {
        int r1, r2;
        double dist;
    };
    std::vector<Pair> pairs;

    for (size_t i = 0; i < routes.size(); ++i) {
        if (routes[i].getNodes().size() <= 2)
            continue;
        auto centroid1 = calculateRouteCentroid(routes[i]);
        for (size_t j = i + 1; j < routes.size(); ++j) {
            if (routes[j].getNodes().size() <= 2)
                continue;
            if (!canPotentiallyMerge(routes[i], routes[j]))
                continue;

            auto centroid2 = calculateRouteCentroid(routes[j]);
            double distance = euclideanDistance(centroid1.first, centroid1.second,
                                                centroid2.first, centroid2.second);
            pairs.push_back({static_cast<int>(i), static_cast<int>(j), distance});
        }
    }

    std::unordered_set<int> routesToRemoveSet;

    std::uniform_real_distribution<double> chanceProb(0.0, 1.0);
    bool useCrossQuad = chanceProb(rng) < 0.5;

    if (useCrossQuad && activeRoutes >= 2) {
        // --- CROSS-QUAD MERGING ---
        // Pick a small route as the base
        std::vector<int> validRoutes;
        for (size_t i = 0; i < routes.size(); ++i) {
            if (routes[i].getNodes().size() > 2) validRoutes.push_back(static_cast<int>(i));
        }
        
        std::sort(validRoutes.begin(), validRoutes.end(), [&](int a, int b) {
            return routes[a].getCustomers().size() < routes[b].getCustomers().size();
        });
        
        std::uniform_int_distribution<int> randBase(0, std::min(2, (int)validRoutes.size() - 1));
        int baseRouteIdx = validRoutes[randBase(rng)];
        routesToRemoveSet.insert(baseRouteIdx);
        
        auto baseCentroid = calculateRouteCentroid(routes[baseRouteIdx]);
        
        struct Cand { int rIdx; double dist; };
        std::vector<Cand> q1, q2, q3, q4;
        std::uniform_real_distribution<double> distNoise(0.8, 1.2);
        
        for (int rIdx : validRoutes) {
            if (rIdx == baseRouteIdx) continue;
            if (!canPotentiallyMerge(routes[baseRouteIdx], routes[rIdx])) continue;
            
            auto centroid = calculateRouteCentroid(routes[rIdx]);
            double dx = centroid.first - baseCentroid.first;
            double dy = centroid.second - baseCentroid.second;
            double dist = euclideanDistance(centroid.first, centroid.second, baseCentroid.first, baseCentroid.second) * distNoise(rng);
            
            if (dx >= 0 && dy >= 0) q1.push_back({rIdx, dist});
            else if (dx < 0 && dy >= 0) q2.push_back({rIdx, dist});
            else if (dx < 0 && dy < 0) q3.push_back({rIdx, dist});
            else q4.push_back({rIdx, dist});
        }
        
        auto sortQ = [](std::vector<Cand>& q) { std::sort(q.begin(), q.end(), [](const Cand& a, const Cand& b){ return a.dist < b.dist; }); };
        sortQ(q1); sortQ(q2); sortQ(q3); sortQ(q4);
        
        std::vector<std::vector<Cand>*> quadrants = {&q1, &q2, &q3, &q4};
        std::shuffle(quadrants.begin(), quadrants.end(), rng);
        
        int qIdx = 0;
        while (routesToRemoveSet.size() < targetRoutesToRemove && qIdx < 4) {
            if (!quadrants[qIdx]->empty()) routesToRemoveSet.insert(quadrants[qIdx]->front().rIdx);
            qIdx++;
        }
    } else {
        // --- STANDARD CENTROID MERGING ---
        if (!pairs.empty()) {
            std::uniform_real_distribution<double> distNoise(0.8, 1.2);
            for (auto &p: pairs) p.dist *= distNoise(rng);

            std::sort(pairs.begin(), pairs.end(),
                      [](const Pair &a, const Pair &b) { return a.dist < b.dist; });

            for (const auto &p: pairs) {
                if (routesToRemoveSet.size() >= targetRoutesToRemove)
                    break;
                routesToRemoveSet.insert(p.r1);
                if (routesToRemoveSet.size() < targetRoutesToRemove) {
                    routesToRemoveSet.insert(p.r2);
                }
            }
        }
    }

    // Fallback: if pairs is empty or didn't reach target, pick random valid
    // routes
    if (routesToRemoveSet.size() < targetRoutesToRemove) {
        std::vector<int> validRoutes;
        for (size_t i = 0; i < routes.size(); ++i) {
            if (routes[i].getNodes().size() > 2)
                validRoutes.push_back(static_cast<int>(i));
        }
        std::shuffle(validRoutes.begin(), validRoutes.end(), rng);
        for (int rIdx: validRoutes) {
            if (routesToRemoveSet.size() >= targetRoutesToRemove)
                break;
            routesToRemoveSet.insert(rIdx);
        }
    }

    // Remove chosen routes (sort descending to avoid index shifting issues)
    std::vector<int> routesToRemove(routesToRemoveSet.begin(),
                                    routesToRemoveSet.end());
    std::sort(routesToRemove.begin(), routesToRemove.end(), std::greater<int>());

    for (int routeIdx: routesToRemove) {
        const auto &nodes = routes[routeIdx].getNodes();
        for (int nodeId: nodes) {
            auto node = instance->getNodeById(nodeId);
            if (node->getType() == NodeType::CUSTOMER) {
                removedCustomers.push_back(nodeId);
            }
        }
        solution.removeRoute(routeIdx);
    }

    return removedCustomers;
}
