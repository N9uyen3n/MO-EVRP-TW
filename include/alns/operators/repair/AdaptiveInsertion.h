#pragma once
#include "../../../core/Instance.h"
#include "../../../core/Solution.h"
#include "../../IOperator.h"
#include <memory>
#include <random>
#include <vector>

namespace alns {

/**
 * @brief Adaptive Insertion Operator (Upgraded)
 *
 * Consolidates multiple greedy insertion strategies:
 *   DISTANCE_FOCUSED, WORKLOAD_FOCUSED, MAXTIME_FOCUSED, VEHICLE_PACKING.
 *
 * VEHICLE_PACKING mode ưu tiên nhồi khách vào route gần đầy nhất,
 * kết hợp Station-Assisted Fallback (Keskin & Çatay 2016) để giảm xe.
 *
 * Mode selection is biased by the current weight vector from ALNSSolver,
 * ensuring coordination between operator behavior and solver objectives.
 */
class AdaptiveInsertion : public IRepairOperator {
public:
  explicit AdaptiveInsertion(std::shared_ptr<Instance> instance);

  std::string getName() const override;

  void execute(Solution &solution, const std::vector<int> &unservedCustomers,
               std::mt19937 &rng) override;

  // --- Weight Hint Interface ---
  struct WeightHint {
    double dist = 0.33, gini = 0.33, time = 0.34;
  };

  void setWeightHint(double dist, double gini, double time);

  // Trong public section:
  void setVehicleReductionMode(bool active);

private:
  std::shared_ptr<Instance> instance;
  WeightHint weightHint_;

  // Trong private section:
  bool vehicleReductionMode_ = false;

  enum class Mode {
    WORKLOAD_FOCUSED,
    MAXTIME_FOCUSED,
    DISTANCE_FOCUSED,
    VEHICLE_PACKING
  };

  double calculateCost(const InsertionResult &result, Mode mode,
                       const Route &route, double meanRouteDuration,
                       double maxRouteDuration, int customerId, int position,
                       double avgRouteDistance, double timeHorizon) const;
};

} // namespace alns
