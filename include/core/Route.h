#pragma once
#include "Instance.h"
#include "Node.h"
#include "Vehicle.h"
#include <memory>
#include <stdexcept>
#include <vector>

struct NodeState {
  double arrivalTime;
  double departureTime;
  double timeWait;
  double remainingBattery;
  double remainingLoad;
  double chargeAmount;
};

struct EvaluationResult {
  double totalDistance = 0.0;
  double totalTime = 0.0;
  double totalWaitTime = 0.0;
  double totalChargeTime = 0.0;
  double totalChargeAmount = 0.0;
  double totalEnergyConsumption = 0.0;
  bool feasible = true;

  EvaluationResult(double dist, double time, double wait, double chargeT,
                   double chargeA, double energyC, bool feas)
      : totalDistance(dist), totalTime(time), totalWaitTime(wait),
        totalChargeTime(chargeT), totalChargeAmount(chargeA),
        totalEnergyConsumption(energyC), feasible(feas) {}

  EvaluationResult() = default;
};

struct InsertionResult {
  bool isFeasible = false;
  double deltaDistance = 0.0;
  double deltaChargeAmount = 0.0;
  double deltaWaitTime = 0.0;
  double deltaEnergyConsumption = 0.0;
};

class Route {
public:
  Route(int id, std::shared_ptr<Vehicle> vehicle,
        const std::shared_ptr<Instance> &instance);

  int getId() const;
  std::shared_ptr<Vehicle> getVehicle() const;
  void addNode(int nodeId, size_t position);
  void addNode(int nodeId);
  void removeNode(size_t position);
  void clear();
  void reverseNodes(size_t i, size_t j);

  const std::vector<int> &getNodes() const;
  std::vector<int> getCustomers()
      const; // Trả về danh sách ID khách hàng (không bao gồm station và depot)
  bool isFeasible() const;
  double getTotalDistance() const;
  double getTotalWaitTime() const;
  double getTotalChargeTime() const;
  double getTotalChargeAmount() const;
  double getTotalEnergyConsumption() const;
  double getTotalTime() const;
  double getActiveTime() const; // New: TotalTime - WaitTime
  double getTotalDemand() const;
  void evaluate() const;
  const std::vector<NodeState> &getStates() const;
  int getNodeAt(size_t pos) const;
  int getLastNodeId() const;
  size_t size() const;

  void print() const;
  std::string toString() const;
  long long getHash() const;

  InsertionResult checkInsertionCost(int nodeId, size_t position) const;
  bool canPossiblyInsert(int nodeIdToInsert, size_t position,
                         int nodeIdToRemove = -1) const;
  InsertionResult fastForwardCheck(int nodeId, size_t position) const;
  bool quickCapacityCheck(double demand) const;

  // Backward Pass minimum battery requirement getter
  const std::vector<double> &getMinBatteryReq() const;

private:
  void detach() const;
  void detachStates() const;

  int id;
  std::shared_ptr<Vehicle> vehicle;
  std::shared_ptr<Instance> instance;

  // --- Data with Copy-On-Write ---
  mutable std::shared_ptr<std::vector<int>> nodeSequence;
  mutable std::shared_ptr<std::vector<NodeState>> states;
  mutable std::shared_ptr<std::vector<double>>
      minBatteryReq; // Backward Pass result

  // --- Mutable members for Lazy Evaluation ---
  mutable EvaluationResult evalResult;
  mutable bool isDirty = true;
};
