#pragma once

#include "../../../alns/IOperator.h"
#include "../../../core/Instance.h"
#include "../../../core/Solution.h"
#include <vector>
#include <memory>
#include <random>

/**
 * VehiclePackingRepair — Phase 4 Rewrite
 *
 * Repair operator with packing intent:
 *  1. Sort unserved customers by TW tightness (dueDate - readyTime, asc)
 *  2. For each customer, scan all routes and positions
 *  3. Packing score = deltaDistance - PACK_BONUS * (activeTime / horizon)
 *  4. Fallback: top-3 nearest stations assisted insertion
 *  5. Last resort: create new route (with station if needed)
 */
class VehiclePackingRepair : public IRepairOperator {
public:
  explicit VehiclePackingRepair(std::shared_ptr<Instance> instance);

  std::string getName() const override;
  void execute(Solution& solution, const std::vector<int>& unservedCustomers, std::mt19937& rng) override;

private:
  std::shared_ptr<Instance> instance_;
};
