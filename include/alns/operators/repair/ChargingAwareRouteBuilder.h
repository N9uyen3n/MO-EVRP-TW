#pragma once

#include "alns/IOperator.h"
#include "core/Instance.h"
#include "core/Solution.h"
#include <memory>
#include <random>
#include <set>
#include <unordered_set>
#include <vector>

namespace alns {

class ChargingAwareRouteBuilder : public ::IRepairOperator {
public:
  explicit ChargingAwareRouteBuilder(std::shared_ptr<Instance> instance);
  ~ChargingAwareRouteBuilder() override = default;

  void execute(Solution &solution, const std::vector<int> &unservedCustomers,
               std::mt19937 &rng) override;
  std::string getName() const override { return "ChargingAwareRouteBuilder"; }

private:
  std::shared_ptr<Instance> instance_;
  double avgDemand_;
  std::vector<int> stationIds_;

  static constexpr int MAX_DEPTH_CAP = 12;
  static constexpr int INNER_BEAM_WIDTH = 15;
  static constexpr int OUTER_BEAM_WIDTH = 20;
  static constexpr int MAX_ATTEMPTS = 20;
  static constexpr int FAIL_THRESHOLD = 3;
  static constexpr double TARGET_BONUS = 50.0;

  struct OuterState {
    std::vector<int> nodeSequence;
    int currentAnchor;
    double currentTime;
    double currentBattery;
    double currentLoad;
    std::unordered_set<int> coveredCusts;
    double segmentCost;
    double pruneScore;
  };

  struct InnerState {
    int position;
    double battery;
    double time;
    double load;
    std::vector<int> visited;
    double cost;
  };

  struct Segment {
    int entryAnchor;
    int exitAnchor;
    std::vector<int> customers;
    double cost;
    double arrivalAtExit;
    double batteryAtExit;
  };

  struct InsertCandidate {
    int custId;
    int routeIdx;
    int pos;
    double cost;
    int stationId; // -1 if no station added
    bool statBefore;
  };

  Route buildRouteFromSequence(const std::vector<int> &seq) const;
  int findNearestStation(int nodeId) const;
  double totalDemandOf(const std::vector<int> &customers) const;
  std::vector<Segment>
  generateSegmentsFrom(int anchorId,
                       const std::unordered_set<int> &remainingCusts,
                       const OuterState &state) const;
  Route buildOneRoute(int depotId, int targetCust,
                      const std::unordered_set<int> &remaining,
                      std::mt19937 &rng) const;
  InsertCandidate findBestInsertForCustomer(int custId,
                                            std::vector<Route> &routes) const;
  void applyCandidate(const InsertCandidate &c,
                      std::vector<Route> &routes) const;
};

} // namespace alns
