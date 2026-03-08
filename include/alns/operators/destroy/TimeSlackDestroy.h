#pragma once

#include "alns/IOperator.h"
#include "core/Instance.h"
#include "core/Solution.h"
#include <memory>
#include <random>
#include <unordered_map>
#include <vector>

namespace alns {

class TimeSlackDestroy : public ::IDestroyOperator {
public:
  explicit TimeSlackDestroy(std::shared_ptr<Instance> instance,
                            double explorationFactor = 3.0);
  ~TimeSlackDestroy() override = default;

  std::vector<int> execute(Solution &solution, int nRemove,
                           std::mt19937 &rng) override;

  void setExplorationFactor(double f) { explorationFactor_ = f; }
  std::string getName() const override { return "TimeSlackDestroy"; }

private:
  std::shared_ptr<Instance> instance_;
  double explorationFactor_;

  static constexpr double EPSILON = 1e-6;
  static constexpr double ALPHA = 0.5;
  static constexpr double BETA = 0.3;
  static constexpr double GAMMA = 0.2;
  static constexpr int TOP_K = 10;

  struct RouteTimingData {
    std::vector<double> arrivalTime;
    std::vector<double> serviceStart;
    std::vector<double> slack;
    std::vector<double> fts;
    std::vector<int> custPositions;
  };

  struct CandidateEntry {
    double score;
    int routeIdx;
    int posInRoute;
  };

  RouteTimingData computeTimingData(const Route &route) const;
  double computeLiberationScore(const RouteTimingData &data, int k,
                                const Route &route) const;
};

} // namespace alns
