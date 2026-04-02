#pragma once

#include "../../../alns/IOperator.h"
#include "../../../core/Instance.h"
#include "../../../core/Solution.h"
#include <vector>
#include <memory>
#include <random>

/**
 * VehicleReductionAwareDestroy — Phase 3 Rewrite
 *
 * Detects instance profile (TW_SATURATION_BREAK / ANCHOR_UNLOCK / FORCED_VICTIM_REMOVAL)
 * and removes all customers from the smallest (victim) route.
 * Additionally removes 1-2 profile-specific extras to enable vehicle reduction.
 */
class VehicleReductionAwareDestroy : public IDestroyOperator {
public:
  explicit VehicleReductionAwareDestroy(std::shared_ptr<Instance> instance);

  std::vector<int> execute(Solution& solution, int nodesToRemove, std::mt19937& rng) override;
  std::string getName() const override;

 // Diversity tracking
 DestroyOperatorType getDestroyType() const override { return DestroyOperatorType::VEHICLE_REDUCTION; }

private:
  std::shared_ptr<Instance> instance_;

  enum class InstanceProfile {
    TW_SATURATION_BREAK,   // tight TW slots — remove low-FTS customers from overlapping bands
    ANCHOR_UNLOCK,         // anchor-heavy — remove light-demand anchors from small routes
    FORCED_VICTIM_REMOVAL, // default — just evict victim route customers
  };

  InstanceProfile detectProfile(const Solution& solution) const;
  int findVictimRoute(const Solution& solution) const;
};
