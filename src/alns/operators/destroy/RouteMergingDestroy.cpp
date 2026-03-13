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

bool RouteMergingDestroy::canPotentiallyMerge(const Route &r1,
                                               const Route &r2) const {
    // Check 1: Capacity — nới lỏng lên 1.8×
    double totalDemand = getTotalDemand(r1) + getTotalDemand(r2);
    if (totalDemand > instance->getVehicleCapacity() * 1.8)
        return false;

    // Check 2: Pairwise TW compatibility (thay thế union/span check cũ)
    //
    // Vấn đề của span check cũ: dùng union [min_ready, max_due] của cả route →
    // false positive cao. Ví dụ route A có [10,20] và [100,110], route B có
    // [50,120] → span overlap nhưng [10,20] và [50,120] conflict hoàn toàn.
    //
    // Fix: đếm tỷ lệ cặp customer (ci ∈ r1, cj ∈ r2) có TW tương thích.
    // Hai customers tương thích nếu có thể phục vụ tuần tự trong cùng shift:
    //   max(ready_i, ready_j) < min(due_i, due_j)   (TW giao nhau)
    //   HOẶC một trong hai có thể đến sau khi hoàn thành cái kia trong horizon.
    // Đơn giản hóa: chỉ cần TW của ci và cj không hoàn toàn tách rời.
    // "Tách rời" = due_i < ready_j VÀ due_j < ready_i (không ai chờ được ai).
    //
    // Nếu > 50% cặp conflict → routes này không hợp → skip.
    // Với routes nhỏ (≤ 5 customers mỗi route) thì check toàn bộ O(25) cặp.
    // Với routes lớn hơn, sample tối đa 5×5 = 25 cặp để tránh O(N²).
    const auto &custs1 = r1.getCustomers();
    const auto &custs2 = r2.getCustomers();

    if (custs1.empty() || custs2.empty()) return true; // không check được → cho qua

    // Lấy tối đa 5 customers đại diện từ mỗi route
    int n1 = std::min((int)custs1.size(), 5);
    int n2 = std::min((int)custs2.size(), 5);

    int totalPairs   = 0;
    int conflictPairs = 0;

    for (int i = 0; i < n1; ++i) {
        auto nodeA = instance->getNodeById(custs1[i]);
        double readyA = nodeA->getReadyTime();
        double dueA   = nodeA->getDueDate();

        for (int j = 0; j < n2; ++j) {
            auto nodeB = instance->getNodeById(custs2[j]);
            double readyB = nodeB->getReadyTime();
            double dueB   = nodeB->getDueDate();

            // Conflict: A hoàn toàn trước B VÀ B hoàn toàn trước A
            // → không thể đi cùng route theo bất kỳ thứ tự nào trong TW
            // (bỏ qua travel time để check nhanh — conservative nhưng đủ)
            bool conflict = (dueA < readyB) && (dueB < readyA);
            if (conflict) conflictPairs++;
            totalPairs++;
        }
    }

    // Nếu > 60% cặp sample conflict → không nên merge
    if (totalPairs > 0 &&
        (double)conflictPairs / totalPairs > 0.60)
        return false;

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
        std::vector<int> validRoutes;
        for (size_t i = 0; i < routes.size(); ++i) {
            if (routes[i].getNodes().size() > 2) validRoutes.push_back(static_cast<int>(i));
        }

        // ⭐ Guard: nếu validRoutes rỗng thì không có gì để remove
        if (validRoutes.empty()) goto fallback_random;

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
    fallback_random:
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