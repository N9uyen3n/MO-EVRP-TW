#pragma once

#include "../../IOperator.h"
#include "../../../../include/core/Instance.h"
#include "../../../../include/core/Route.h"
#include "../../../../include/core/Solution.h"
#include "../../../../include/core/Vehicle.h"
#include <memory>
#include <random>
#include <string>
#include <vector>

// ============================================================================
// RegretKRepair — Regret-K Insertion Repair Operator
// ============================================================================
// Với mỗi unserved customer, tính regret = sum(cost_rank_i - cost_rank_1)
// cho i=2..K trên EXISTING routes. Customer có regret cao nhất được insert
// trước (nếu trì hoãn sẽ mất nhiều nhất).
//
// Khác với version cũ:
//   - Không dùng fastForwardCheck (bug timeWait + 1.1x overcharge)
//   - newRoute không tham gia regret calculation (tránh distortion)
//   - Cost formula nhận weight hint từ ALNS (aligned với Pareto objective)
//   - setWeightHint() cho phép ALNS wire objective weights giống AdaptiveInsertion
// ============================================================================
class RegretKRepair : public IRepairOperator {
public:
  RegretKRepair(std::shared_ptr<Instance> instance, int k,
                double noiseParameter = 0.0);

  std::string getName() const override;

  void execute(Solution& solution, const std::vector<int>& unservedCustomers, std::mt19937& rng) override;

  // Nhận weight hint từ ALNS (giống AdaptiveInsertion)
  void setWeightHint(double wDist, double wGini, double wTime);

  // Backward compat: noise parameter setter
  void setNoiseParameter(double noise) { noiseParam = noise; }

private:
  std::shared_ptr<Instance> instance;
  int    k_regret;
  double noiseParam;

  // Weight hints từ ALNS (wired per-iteration)
  double wDist_ = 1.0;
  double wGini_ = 0.0;
  double wTime_ = 0.0;

  struct InsertionCost {
    int    routeIndex;
    int    position;
    double cost;
    bool   isFeasible;
  };

  // Tìm top-K insertions vào existing routes (KHÔNG bao gồm new-route option)
  std::vector<InsertionCost> findKBestInsertions(int custId,
                                                  Solution &solution,
                                                  std::mt19937 &rng,
                                                  int K);
};