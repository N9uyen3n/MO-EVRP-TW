#pragma once
#include <cmath>
#include <algorithm>
#include "../core/Solution.h"

namespace alns {

/**
 * @brief Evaluator for multi-objective optimization with Adaptive Scalarization.
 * 
 * Handles normalization and dynamic weighting for:
 * 1. Total Vehicles (Hierarchical priority)
 * 2. Total Distance
 * 3. Driver Equity (Gini Coefficient)
 */
class ObjectiveEvaluator {
public:
    ObjectiveEvaluator();

    /**
     * @brief Compute scalarized cost for a solution at a specific iteration.
     * Cost = w_V * Z_veh + w_D * Z_dist + w_F(t) * Z_gini
     */
    double computeScalarCost(const Solution& sol, int iteration);

    /**
     * @brief Set reference values for normalization.
     * Usually based on the initial solution or best known solution.
     */
    void setReferences(int refVehicles, double refDistance, double refGini);

    /**
     * @brief Configure adaptive strategy parameters.
     * @param lambdaStart Initial fairness weight
     * @param lambdaEnd Final fairness weight
     * @param tau Transition ratio (0.0 - 1.0)
     * @param maxIterations Total iterations
     */
    void setAdaptiveParams(double lambdaStart, double lambdaEnd, double tau, int maxIterations);

    /**
     * @brief Get the current fairness weight for the given iteration.
     */
    double getCurrentFairnessWeight(int iteration) const;

private:
    // Reference values for normalization
    int refVehicles_;
    double refDistance_;
    double refGini_;

    // Adaptive parameters
    double lambdaStart_;
    double lambdaEnd_;
    double tau_;        // Transition ratio
    int maxIterations_;

    // Weight constants
    double w_V_;  // Vehicle weight (default: 1000.0)
    double w_D_;  // Distance weight (default: 1.0)
};

} // namespace alns
