#pragma once
#include "../../IOperator.h"
#include "../../../core/Instance.h"
#include "../../../core/Solution.h"
#include <memory>
#include <vector>
#include <random>

namespace alns {

/**
 * @brief Adaptive Insertion Operator
 * 
 * Consolidates multiple greedy insertion strategies (Distance, Workload, MaxTime).
 * Mode selection is biased by the current weight vector from ALNSSolver,
 * ensuring coordination between operator behavior and solver objectives.
 */
class AdaptiveInsertion : public IRepairOperator {
public:
    explicit AdaptiveInsertion(std::shared_ptr<Instance> instance);

    std::string getName() const override;

    void execute(Solution& solution, const std::vector<int>& unservedCustomers, std::mt19937& rng) override;

    // --- Weight Hint Interface (Fix 2) ---
    struct WeightHint {
        double dist = 0.33, gini = 0.33, time = 0.34;
    };

    void setWeightHint(double dist, double gini, double time);

private:
    std::shared_ptr<Instance> instance;
    WeightHint weightHint_;

    enum class Mode {
        DISTANCE_FOCUSED,
        WORKLOAD_FOCUSED,
        MAXTIME_FOCUSED
    };

    struct InsertionCandidate {
        int customerId = -1;
        int routeIndex = -1;
        int position = -1;
        double costIncrease = std::numeric_limits<double>::infinity();
    };

    double calculateCost(const InsertionResult& result, Mode mode, const Route& route, double meanRouteDuration, double maxRouteDuration, int customerId) const;
};

} // namespace alns
