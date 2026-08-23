#pragma once
#include "../../../core/Instance.h"
#include "../../../core/Solution.h"
#include "../../../core/Station.h"
#include "../../IOperator.h"
#include <algorithm>
#include <iostream>
#include <memory>
#include <random>
#include <vector>


namespace alns {

/**
 * @brief Targeted Station Removal Destroy Operator
 *
 * Removes charging stations based on an inefficiency score.
 * A high score indicates a "bad" station (candidate for removal).
 *
 * Score Criteria:
 * 1. Low Usage: Station charges very little energy (< 15% capacity).
 * 2. High Detour: Station causes significant distance increase.
 * 3. Redundancy: Station is at position 1 (right after depot) or follows
 * another station.
 */
class TargetedStationRemoval : public IDestroyOperator {
public:
  TargetedStationRemoval(std::shared_ptr<Instance> instance);

  std::string getName() const override;

  std::vector<int> execute(Solution &solution, int nodesToRemove,
                           std::mt19937 &rng) override;

private:
  std::shared_ptr<Instance> instance;

  struct StationCandidate {
    int routeIdx;
    int nodeIdx;
    int stationId;
    double score;

    bool operator>(const StationCandidate &other) const {
      return score > other.score;
    }
  };

  double calculateStationScore(const Route &route, int nodeIdx) const;
};

} // namespace alns
