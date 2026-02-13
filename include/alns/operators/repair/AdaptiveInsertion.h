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
 * Consolidates multiple greedy insertion strategies (Distance, Time, Random/Mixed).
 * Randomly selects a weighting strategy at each execution to diversify search.
 * Strategies:
 * - DISTANCE_FOCUSED: Minimizes distance increase.
 * - TIME_FOCUSED: Minimizes time/waiting increase.
 * - BALANCED: Weighted sum of Distance and Time.
 */
class AdaptiveInsertion : public IRepairOperator {
public:
    explicit AdaptiveInsertion(std::shared_ptr<Instance> instance);

    std::string getName() const override;

    void execute(Solution& solution, const std::vector<int>& unservedCustomers, std::mt19937& rng) override;

private:
    std::shared_ptr<Instance> instance;

    enum class Mode {
        DISTANCE_FOCUSED,
        TIME_FOCUSED,
        BALANCED
    };

    struct InsertionCandidate {
        int customerId = -1;
        int routeIndex = -1;
        int position = -1;
        double cost = std::numeric_limits<double>::infinity();
    };

    double calculateCost(const InsertionResult& result, Mode mode) const;
};

} // namespace alns
