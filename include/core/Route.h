#pragma once
#include "Instance.h"
#include "Node.h"
#include "Vehicle.h"
#include <memory>
#include <stdexcept>
#include <vector>

struct NodeState {
  double arrivalTime    = 0.0;
  double departureTime  = 0.0;
  double timeWait       = 0.0;
  double remainingBattery = 0.0;
  double remainingLoad  = 0.0;
  double chargeAmount   = 0.0;
};

struct EvaluationResult {
  double totalDistance        = 0.0;
  double totalTime            = 0.0;
  double totalWaitTime        = 0.0;
  double totalChargeTime      = 0.0;
  double totalChargeAmount    = 0.0;
  double totalEnergyConsumption = 0.0;
  bool   feasible             = true;

  EvaluationResult(double dist, double time, double wait, double chargeT,
                   double chargeA, double energyC, bool feas)
      : totalDistance(dist), totalTime(time), totalWaitTime(wait),
        totalChargeTime(chargeT), totalChargeAmount(chargeA),
        totalEnergyConsumption(energyC), feasible(feas) {}

  EvaluationResult() = default;
};

struct InsertionResult {
  bool   isFeasible           = false;
  double deltaDistance        = 0.0;
  double deltaTime            = 0.0;
  double deltaChargeAmount    = 0.0;
  double deltaWaitTime        = 0.0;
  double deltaEnergyConsumption = 0.0;
};

struct InsertionTimeBoundsStats {
  unsigned long long requests = 0, builds = 0, rejections = 0;
};

class Route {
public:
  Route(int id, std::shared_ptr<Vehicle> vehicle,
        const std::shared_ptr<Instance> &instance);

  // --- Mutators ---
  void addNode(int nodeId, size_t position);
  void addNode(int nodeId);
  void removeNode(size_t position);
  void clear();
  void reverseNodes(size_t i, size_t j);

  // --- Accessors (public interface unchanged) ---
  int getId() const;
  std::shared_ptr<Vehicle> getVehicle() const;
  const std::vector<int> &getNodes() const;
  std::vector<int> getCustomers() const;
  bool isFeasible() const;
  double getTotalDistance() const;
  double getTotalWaitTime() const;
  double getTotalChargeTime() const;
  double getTotalChargeAmount() const;
  double getTotalEnergyConsumption() const;
  double getTotalTime() const;
  double getActiveTime() const;
  double getTotalDemand() const;
  void evaluate() const; // Completes the canonical schedule before any getter returns.
  const std::vector<NodeState> &getStates() const;
  int getNodeAt(size_t pos) const;
  int getLastNodeId() const;
  size_t size() const;

  double getCentroidX() const;
  double getCentroidY() const;

  void print() const;
  std::string toString() const;
  long long getHash() const;

  InsertionResult checkInsertionCost(int nodeId, size_t position) const;
  static bool insertionTimeBoundsEnabled();
  static InsertionTimeBoundsStats getInsertionTimeBoundsStats();
  static void resetInsertionTimeBoundsStats();
  bool canPossiblyInsert(int nodeIdToInsert, size_t position,
                         int nodeIdToRemove = -1) const;
  InsertionResult fastForwardCheck(int nodeId, size_t position) const;
  bool quickCapacityCheck(double demand) const;

  // --- Energy / Time Analysis ---
  std::vector<double> getTimeSlack() const;
  std::vector<double> getEnergySlack() const;
  std::vector<std::pair<int, double>> getBottleneckNodes(double thresholdRatio = 0.15) const;
  std::vector<int> getRedundantStations() const;
  const std::vector<double> &getMinBatteryReq() const;
  void pruneRedundantStations();

private:
#ifdef ALNS_INSERTION_TIME_BOUNDS
  struct InsertionTimeBounds {
    std::vector<double> earliestDeparture, latestStart;
  };
  // Immutable after construction; copied routes share it until either mutates.
  mutable std::shared_ptr<const InsertionTimeBounds> insertionTimeBounds_;
  bool violatesInsertionTimeBounds(int nodeId, size_t position) const;
#endif
  // ── Identity / ownership ──────────────────────────────────────────────────
  int id;
  std::shared_ptr<Vehicle>  vehicle;
  std::shared_ptr<Instance> instance;

  // ── Node sequence (plain vector — no COW) ────────────────────────────────
  mutable std::vector<int>       nodeSequence;
  mutable std::vector<NodeState> states;
  mutable std::vector<double>    minBatteryReq;
  mutable std::vector<double>    timeSlack_;
  mutable std::vector<double>    cumWait_;
  mutable std::vector<bool>      hasStation_;

  // ── Lazy-eval flags ───────────────────────────────────────────────────────
  mutable EvaluationResult evalResult;
  mutable bool isDirty          = true;  // forward+backward pass needed

  // ── Cached derived fields (invalidated on sequence mutation) ──────────────
  mutable double           cachedTotalDemand_    = 0.0;
  mutable bool             cachedDemandDirty_    = true;
  mutable std::vector<int> cachedCustomers_;
  mutable bool             cachedCustomersDirty_ = true;
};
