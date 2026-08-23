#pragma once

#include "../../../alns/IOperator.h"
#include "../../../core/Instance.h"
#include "../../../core/Route.h"
#include <vector>
#include <memory>
#include <random>
#include <unordered_set>

class UnifiedCostDestroy : public IDestroyOperator {
public:
    UnifiedCostDestroy(std::shared_ptr<Instance> instance, int determinism_param = 3);

    std::vector<int> execute(Solution& solution, int nodesToRemove, std::mt19937& rng) override;

    std::string getName() const override;

private:
    std::shared_ptr<Instance> instance;
    int determinism;

    enum CostMetric {
        DISTANCE,
        TIME,
        ENERGY,
        WORKLOAD,
        RANDOM_METRIC
    };
    
    struct RemovalCandidate {
        int nodeId;
        double savings; // Cost saving if removed (higher is better)
        bool operator>(const RemovalCandidate& other) const {
            return savings > other.savings;
        }
    };

    double calculateNodeSaving(const Route& route, size_t position, CostMetric metric, double avgRouteDuration = 0.0);
    double getRouteQualityBonus(const Route& route);
    double getTimeWindowSlack(int customerId, const Route& route);

    // Station-relatedness scoring (from ShawDestroy)
    double calculateStationRelatedness(const Route& route, int customerId) const;
    std::unordered_set<int> getStationNeighbors(const Route& route, int customerId) const;
};
