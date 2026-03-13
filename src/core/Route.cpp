// =============================================================================
// Route.cpp — Optimized
// =============================================================================
// CHANGES vs original:
//
// [OPT-1] Removed COW shared_ptr for nodeSequence / states / minBatteryReq.
//         Plain std::vector with reserve(100) in ctor. Eliminates atomic
//         ref-count inc/dec on every Route copy and use_count() check on every
//         addNode/removeNode. Route copy is now a straightforward std::vector
//         copy — predictable cost, compiler can optimise.
//         detach() and detachStates() removed entirely.
//
// [OPT-2] Slack-Time Absorption extracted to runAbsorptionIfNeeded().
//         evaluate() (the hot path called from isFeasible / getTotalDistance /
//         searchRelocate / searchSwap / …) no longer runs absorption.
//         absorptionDirty_ flag ensures absorption runs exactly once after each
//         sequence mutation, only when a caller actually needs its results.
//         getStates() triggers absorption (callers reading chargeAmount /
//         remainingBattery need it). Callers reading only departureTime get
//         a free ride — branch predicted as not-taken after the first call.
//
// [OPT-3] checkInsertionCost uses simNodeAt lambda instead of O(N) vector
//         copy into tl_simNodeSequence. tl_simNodeSequence removed.
//
// [OPT-4] Consecutive-station sanity check folded into the forward pass
//         (NodeType::STATION case checks nodeSequence[i] == nodeSequence[i+1])
//         so the separate O(N) post-forward loop is gone.
//
// [OPT-A] Toàn bộ hot path (evaluate, checkInsertionCost, fastForwardCheck,
//         runAbsorptionIfNeeded, getCustomers, getTotalDemand, getRedundantStations,
//         getTimeSlack, canPossiblyInsert) dùng instance->getNodeRaw(id) thay vì
//         instance->getNodeById(id) để tránh atomic shared_ptr load/copy.
//         Dùng static_cast<const T*> thay vì static_pointer_cast<T>(shared_ptr)
//         — không tăng refcount, không lock, không branch.
//         Instance sống suốt lifetime của Route nên raw borrow hoàn toàn an toàn.
// =============================================================================

#include "../../include/core/Route.h"
#include "../../include/core/Customer.h"
#include "../../include/core/Depot.h"
#include "../../include/core/Instance.h"
#include "../../include/core/Station.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <ostream>
#include <sstream>
#include <stdexcept>

// =============================================================================
// Constructor
// =============================================================================
Route::Route(int id, std::shared_ptr<Vehicle> vehicle,
             const std::shared_ptr<Instance> &instance)
    : id(id), vehicle(vehicle), instance(instance), evalResult() {
  nodeSequence.reserve(100);
  nodeSequence.push_back(0);
  nodeSequence.push_back(0);
  evaluate();
}

// =============================================================================
// Mutators
// =============================================================================
void Route::addNode(int nodeId, size_t position) {
  if (position < 1 || position > nodeSequence.size() - 1)
    throw std::out_of_range("Vi tri them node khong hop le. Phai nam giua 2 depot.");
  nodeSequence.insert(nodeSequence.begin() + position, nodeId);
  isDirty          = true;
  absorptionDirty_ = true;
  cachedDemandDirty_    = true;
  cachedCustomersDirty_ = true;
}

void Route::addNode(int nodeId) {
  nodeSequence.insert(nodeSequence.end() - 1, nodeId);
  isDirty          = true;
  absorptionDirty_ = true;
  cachedDemandDirty_    = true;
  cachedCustomersDirty_ = true;
}

void Route::removeNode(size_t position) {
  if (position < 1 || position > nodeSequence.size() - 2)
    throw std::out_of_range("Vi tri xoa node khong hop le. Khong the xoa depot.");
  nodeSequence.erase(nodeSequence.begin() + position);
  isDirty          = true;
  absorptionDirty_ = true;
  cachedDemandDirty_    = true;
  cachedCustomersDirty_ = true;
}

void Route::clear() {
  nodeSequence.clear();
  nodeSequence.push_back(0);
  nodeSequence.push_back(0);
  isDirty          = true;
  absorptionDirty_ = true;
  cachedDemandDirty_    = true;
  cachedCustomersDirty_ = true;
  evaluate();
}

void Route::reverseNodes(size_t i, size_t j) {
  if (i < 1 || j >= nodeSequence.size() - 1 || i >= j)
    throw std::out_of_range("Chi so 2-Opt khong hop le.");
  std::reverse(nodeSequence.begin() + i, nodeSequence.begin() + j + 1);
  isDirty          = true;
  absorptionDirty_ = true;
  // demand + customers unchanged by reversal — no cache invalidation needed
}

// =============================================================================
// evaluate() — Backward pass + Forward pass only.
// Slack-Time Absorption is NOT run here; see runAbsorptionIfNeeded().
// Public signature is unchanged: void evaluate() const.
// =============================================================================
void Route::evaluate() const {
  if (!isDirty) return;

  const int n = static_cast<int>(nodeSequence.size());
  if (n <= 1) {
    evalResult = EvaluationResult();
    states.clear();
    minBatteryReq.clear();
    isDirty          = false;
    absorptionDirty_ = true;
    return;
  }

  const double EPSILON = 1e-9;
  const double batCap  = vehicle->getBatteryCapacity();
  const double eRate   = vehicle->getEnergyConsumptionRate();

  // ── Backward pass ─────────────────────────────────────────────────────────
  // [OPT-A] getNodeRaw() thay getNodeById() — tránh atomic shared_ptr load
  minBatteryReq.assign(n, 0.0);
  for (int i = n - 2; i >= 0; --i) {
    double e = instance->getDistance(nodeSequence[i], nodeSequence[i + 1]) * eRate;
    if (instance->getNodeType(nodeSequence[i + 1]) == NodeType::STATION)
      minBatteryReq[i] = e + std::max(0.0, minBatteryReq[i + 1] - batCap);
    else
      minBatteryReq[i] = e + minBatteryReq[i + 1];

    if (minBatteryReq[i] > batCap + EPSILON) {
      evalResult          = EvaluationResult();
      evalResult.feasible = false;
      isDirty             = false;
      absorptionDirty_    = true;
      return;
    }
  }

  // ── Forward pass ──────────────────────────────────────────────────────────
  states.resize(n);
  evalResult = EvaluationResult();

  {
    // [OPT-A] getNodeRaw + static_cast — không copy shared_ptr
    const Depot* depot = static_cast<const Depot*>(instance->getNodeRaw(0));
    states[0].arrivalTime      = depot->getReadyTime();
    states[0].departureTime    = depot->getReadyTime();
    states[0].remainingBattery = batCap;
    states[0].remainingLoad    = vehicle->getCapacity();
    states[0].chargeAmount     = 0.0;
    states[0].timeWait         = 0.0;
  }

  for (int i = 0; i < n - 1; ++i) {
    const int from_id = nodeSequence[i];
    const int to_id   = nodeSequence[i + 1];

    // [OPT-A] getNodeRaw + static_cast — không copy shared_ptr, không tăng refcount
    const Node* to_node_raw = instance->getNodeRaw(to_id);

    NodeState &from = states[i];
    NodeState &to   = states[i + 1];

    double dist   = instance->getDistance(from_id, to_id);
    double ttime  = instance->getTime(from_id, to_id);
    double energy = dist * eRate;

    evalResult.totalDistance          += dist;
    evalResult.totalEnergyConsumption += energy;

    to.arrivalTime      = from.departureTime + ttime;
    to.remainingBattery = from.remainingBattery - energy;
    to.remainingLoad    = from.remainingLoad;
    to.chargeAmount     = 0.0;
    to.timeWait         = 0.0;

    if (to.remainingBattery < -EPSILON) {
      evalResult.feasible = false;
      isDirty = false; absorptionDirty_ = true;
      return;
    }

    switch (to_node_raw->getType()) {
    case NodeType::CUSTOMER: {
      // [OPT-A] static_cast thay vì static_pointer_cast — không động đến refcount
      const Customer* cust = static_cast<const Customer*>(to_node_raw);
      to.remainingLoad -= cust->getDemand();
      if (to.remainingLoad < -EPSILON) {
        evalResult.feasible = false;
        isDirty = false; absorptionDirty_ = true;
        return;
      }
      double wait = std::max(0.0, cust->getReadyTime() - to.arrivalTime);
      to.timeWait = wait;
      evalResult.totalWaitTime += wait;
      double svcStart = to.arrivalTime + wait;
      if (svcStart > cust->getDueDate() + EPSILON) {
        evalResult.feasible = false;
        isDirty = false; absorptionDirty_ = true;
        return;
      }
      to.departureTime = svcStart + cust->getServiceTime();
      break;
    }
    case NodeType::STATION: {
      // [OPT-4] Consecutive-same-station check inline (giữ nguyên)
      if (i > 0 && nodeSequence[i] == nodeSequence[i + 1]) {
        evalResult.feasible = false;
        isDirty = false; absorptionDirty_ = true;
        return;
      }
      const Station* station = static_cast<const Station*>(to_node_raw);
      double wait = std::max(0.0, station->getReadyTime() - to.arrivalTime);
      to.timeWait = wait;
      evalResult.totalWaitTime += wait;
      double chargeStart = to.arrivalTime + wait;
      if (chargeStart > station->getDueDate() + EPSILON) {
        evalResult.feasible = false;
        isDirty = false; absorptionDirty_ = true;
        return;
      }
      double chargeNeeded   = std::max(0.0, minBatteryReq[i + 1] - to.remainingBattery);
      double chargePossible = batCap - to.remainingBattery;
      double chargeAmt      = std::min(chargeNeeded, chargePossible);
      if (chargeAmt < chargeNeeded - EPSILON) {
        evalResult.feasible = false;
        isDirty = false; absorptionDirty_ = true;
        return;
      }
      double chargeTime = chargeAmt * station->getChargingRate();
      to.chargeAmount      = chargeAmt;
      to.remainingBattery += chargeAmt;
      to.departureTime     = chargeStart + chargeTime;
      evalResult.totalChargeTime   += chargeTime;
      evalResult.totalChargeAmount += chargeAmt;
      if (to.departureTime > station->getDueDate() + EPSILON) {
        evalResult.feasible = false;
        isDirty = false; absorptionDirty_ = true;
        return;
      }
      break;
    }
    case NodeType::DEPOT: {
      const Depot* depot = static_cast<const Depot*>(to_node_raw);
      if (to.arrivalTime > depot->getDueDate() + EPSILON) {
        evalResult.feasible = false;
        isDirty = false; absorptionDirty_ = true;
        return;
      }
      to.departureTime = to.arrivalTime;
      const_cast<Depot*>(depot)->setLastTime(to.arrivalTime);
      break;
    }
    }
  }

  evalResult.feasible = true;
  evalResult.totalTime = states.back().arrivalTime - states.front().departureTime;
  isDirty          = false;
  absorptionDirty_ = true;
}

// =============================================================================
// runAbsorptionIfNeeded() — Slack-Time Absorption (internal, lazy).
// Precondition: evaluate() has already been called (isDirty == false).
// Only modifies chargeAmount / remainingBattery / departureTime in states and
// the charge-related totals in evalResult.
// Must NOT be called if evalResult.feasible == false.
// =============================================================================
void Route::runAbsorptionIfNeeded() const {
  if (!absorptionDirty_) return;
  absorptionDirty_ = false;

  if (!evalResult.feasible) return;

  const int    n       = static_cast<int>(nodeSequence.size());
  const double EPSILON = 1e-9;
  const double batCap  = vehicle->getBatteryCapacity();

  int    last_station_idx      = -1;
  double station_bat_snapshot  = 0.0;

  for (int i = 1; i < n; ++i) {
    // [OPT-A] getNodeRaw — tránh shared_ptr copy
    const Node* node = instance->getNodeRaw(nodeSequence[i]);

    if (node->getType() == NodeType::STATION) {
      last_station_idx     = i;
      station_bat_snapshot = states[i].remainingBattery;
      continue;
    }
    if (node->getType() != NodeType::CUSTOMER) continue;
    if (last_station_idx == -1)               continue;

    double wait = states[i].timeWait;
    if (wait < EPSILON) continue;

    const int s = last_station_idx;
    // [OPT-A] static_cast thay vì static_pointer_cast
    const Station* station_node = static_cast<const Station*>(instance->getNodeRaw(nodeSequence[s]));
    double charging_rate = station_node->getChargingRate();

    double max_extra_energy = batCap - station_bat_snapshot;
    if (max_extra_energy < EPSILON) continue;

    double max_extra_time        = max_extra_energy * charging_rate;
    double time_before_due       = std::max(0.0, station_node->getDueDate()
                                            - states[s].departureTime);
    double extra_charge_time     = std::min({wait, max_extra_time, time_before_due});
    if (extra_charge_time < EPSILON) continue;

    double extra_energy = extra_charge_time / charging_rate;
    station_bat_snapshot += extra_energy;

    // Update station state
    states[s].chargeAmount      += extra_energy;
    states[s].remainingBattery  += extra_energy;
    states[s].departureTime     += extra_charge_time;
    evalResult.totalChargeTime  += extra_charge_time;
    evalResult.totalChargeAmount += extra_energy;

    // Ripple time shift from s+1 to i
    double time_shift = extra_charge_time;
    for (int k = s + 1; k <= i; ++k) {
      if (time_shift < EPSILON) break;
      // [OPT-A] getNodeRaw + static_cast
      const Node* node_k = instance->getNodeRaw(nodeSequence[k]);
      states[k].arrivalTime += time_shift;

      if (node_k->getType() == NodeType::CUSTOMER) {
        const Customer* cust_k = static_cast<const Customer*>(node_k);
        double new_arr = states[k].arrivalTime;
        double old_wait = states[k].timeWait;
        double new_wait = std::max(0.0, cust_k->getReadyTime() - new_arr);
        double absorbed = old_wait - new_wait;
        time_shift = std::max(0.0, time_shift - absorbed);

        evalResult.totalWaitTime -= old_wait;
        evalResult.totalWaitTime += new_wait;
        states[k].timeWait       = new_wait;
        states[k].departureTime  = new_arr + new_wait + cust_k->getServiceTime();
      } else {
        states[k].departureTime += time_shift;
      }
    }

    // Ripple battery from s+1 to i
    for (int k = s + 1; k <= i; ++k)
      states[k].remainingBattery += extra_energy;
  }
}

// =============================================================================
// Thread-local scratchpads (checkInsertionCost + fastForwardCheck)
// =============================================================================
thread_local static std::vector<double>    tl_dp;
thread_local static std::vector<NodeState> tl_simStates;

// =============================================================================
// checkInsertionCost — Tier 1 exact feasibility + delta computation.
// [OPT-3] Uses simNodeAt lambda instead of O(N) vector copy.
// =============================================================================
InsertionResult Route::checkInsertionCost(int nodeId, size_t position) const {
  evaluate();

  // Virtual sequence: nodeSequence[0..position-1] + nodeId + nodeSequence[position..]
  // No allocation needed — index mapping via lambda.
  const int origN = static_cast<int>(nodeSequence.size());
  const int n     = origN + 1;

  auto simNodeAt = [&](int i) -> int {
    if (i < static_cast<int>(position)) return nodeSequence[i];
    if (i == static_cast<int>(position)) return nodeId;
    return nodeSequence[i - 1];
  };

  const double EPSILON = 1e-9;
  const double batCap  = vehicle->getBatteryCapacity();
  const double eRate   = vehicle->getEnergyConsumptionRate();

  // Backward pass
  tl_dp.assign(n, 0.0);
  for (int i = n - 2; i >= 0; --i) {
    double e    = instance->getDistance(simNodeAt(i), simNodeAt(i + 1)) * eRate;
    // [OPT-A] getNodeType — flat array lookup, tránh shared_ptr hoàn toàn
    if (instance->getNodeType(simNodeAt(i + 1)) == NodeType::STATION)
      tl_dp[i] = e + std::max(0.0, tl_dp[i + 1] - batCap);
    else
      tl_dp[i] = e + tl_dp[i + 1];
    if (tl_dp[i] > batCap + EPSILON) return {false};
  }

  // Forward pass
  tl_simStates.resize(n);
  double simDist = 0.0, simWait = 0.0, simCharge = 0.0, simEnergy = 0.0;

  {
    // [OPT-A] getNodeRaw + static_cast
    const Depot* depot = static_cast<const Depot*>(instance->getNodeRaw(0));
    tl_simStates[0].arrivalTime      = depot->getReadyTime();
    tl_simStates[0].departureTime    = depot->getReadyTime();
    tl_simStates[0].remainingBattery = batCap;
    tl_simStates[0].remainingLoad    = vehicle->getCapacity();
    tl_simStates[0].chargeAmount     = 0.0;
  }

  for (int i = 0; i < n - 1; ++i) {
    int from_id  = simNodeAt(i);
    int to_id    = simNodeAt(i + 1);
    // [OPT-A] getNodeRaw + static_cast — tránh shared_ptr copy
    const Node* to_node = instance->getNodeRaw(to_id);
    NodeState &from = tl_simStates[i];
    NodeState &to   = tl_simStates[i + 1];

    double dist   = instance->getDistance(from_id, to_id);
    double ttime  = instance->getTime(from_id, to_id);
    double energy = dist * eRate;

    simDist   += dist;
    simEnergy += energy;

    to.arrivalTime      = from.departureTime + ttime;
    to.remainingBattery = from.remainingBattery - energy;
    to.remainingLoad    = from.remainingLoad;
    to.chargeAmount     = 0.0;

    if (to.remainingBattery < -EPSILON) return {false};

    switch (to_node->getType()) {
    case NodeType::CUSTOMER: {
      // [OPT-A] static_cast thay vì static_pointer_cast
      const Customer* cust = static_cast<const Customer*>(to_node);
      to.remainingLoad -= cust->getDemand();
      if (to.remainingLoad < -EPSILON) return {false};
      double wait = std::max(0.0, cust->getReadyTime() - to.arrivalTime);
      simWait += wait;
      double svcStart = to.arrivalTime + wait;
      if (svcStart > cust->getDueDate() + EPSILON) return {false};
      to.departureTime = svcStart + cust->getServiceTime();
      break;
    }
    case NodeType::STATION: {
      const Station* station = static_cast<const Station*>(to_node);
      double wait = std::max(0.0, station->getReadyTime() - to.arrivalTime);
      simWait += wait;
      double chargeStart = to.arrivalTime + wait;
      if (chargeStart > station->getDueDate() + EPSILON) return {false};
      double chargeNeeded   = std::max(0.0, tl_dp[i + 1] - to.remainingBattery);
      double chargePossible = batCap - to.remainingBattery;
      double chargeAmt      = std::min(chargeNeeded, chargePossible);
      if (chargeAmt < chargeNeeded - EPSILON) return {false};
      double chargeTime = chargeAmt * station->getChargingRate();
      to.chargeAmount     = chargeAmt;
      simCharge           += chargeAmt;
      to.remainingBattery += chargeAmt;
      to.departureTime    = chargeStart + chargeTime;
      if (to.departureTime > station->getDueDate() + EPSILON) return {false};
      break;
    }
    case NodeType::DEPOT: {
      const Depot* depot = static_cast<const Depot*>(to_node);
      if (to.arrivalTime > depot->getDueDate() + EPSILON) return {false};
      to.departureTime = to.arrivalTime;
      break;
    }
    }
  }

  InsertionResult res;
  res.isFeasible              = true;
  res.deltaDistance           = simDist   - evalResult.totalDistance;
  res.deltaTime               = (tl_simStates[n-1].arrivalTime - tl_simStates[0].departureTime)
                                - evalResult.totalTime;
  res.deltaChargeAmount       = simCharge - evalResult.totalChargeAmount;
  res.deltaWaitTime           = simWait   - evalResult.totalWaitTime;
  res.deltaEnergyConsumption  = simEnergy - evalResult.totalEnergyConsumption;
  return res;
}

// =============================================================================
// Getters
// =============================================================================
int Route::getId() const { return id; }

std::shared_ptr<Vehicle> Route::getVehicle() const { return vehicle; }

const std::vector<int> &Route::getNodes() const { return nodeSequence; }

std::vector<int> Route::getCustomers() const {
  if (!cachedCustomersDirty_) return cachedCustomers_;
  cachedCustomers_.clear();
  for (int nid : nodeSequence)
    // [OPT-A] getNodeType — flat array lookup
    if (instance->getNodeType(nid) == NodeType::CUSTOMER)
      cachedCustomers_.push_back(nid);
  cachedCustomersDirty_ = false;
  return cachedCustomers_;
}

bool Route::isFeasible() const {
  evaluate();
  return evalResult.feasible;
}

double Route::getTotalDistance() const {
  evaluate();
  return evalResult.totalDistance;
}

double Route::getTotalWaitTime() const {
  evaluate();
  return evalResult.totalWaitTime;
}

double Route::getTotalChargeTime() const {
  evaluate();
  runAbsorptionIfNeeded();
  return evalResult.totalChargeTime;
}

double Route::getTotalChargeAmount() const {
  evaluate();
  runAbsorptionIfNeeded();
  return evalResult.totalChargeAmount;
}

double Route::getTotalEnergyConsumption() const {
  evaluate();
  return evalResult.totalEnergyConsumption;
}

double Route::getTotalTime() const {
  evaluate();
  return evalResult.totalTime;
}

double Route::getActiveTime() const {
  evaluate();
  return evalResult.totalTime - evalResult.totalWaitTime;
}

double Route::getTotalDemand() const {
  if (!cachedDemandDirty_) return cachedTotalDemand_;
  double total = 0.0;
  for (int nid : nodeSequence)
    // [OPT-A] getDemand() trả 0.0 cho non-customer — getNodeRaw đủ dùng
    total += instance->getNodeRaw(nid)->getDemand();
  cachedTotalDemand_ = total;
  cachedDemandDirty_ = false;
  return total;
}

// getStates() triggers absorption — callers reading chargeAmount /
// remainingBattery need up-to-date values.
const std::vector<NodeState> &Route::getStates() const {
  evaluate();
  runAbsorptionIfNeeded();
  return states;
}

int Route::getNodeAt(size_t pos) const {
  if (pos >= nodeSequence.size()) return -1;
  return nodeSequence[pos];
}

int Route::getLastNodeId() const {
  if (nodeSequence.size() <= 2) return 0;
  return nodeSequence[nodeSequence.size() - 2];
}

double Route::getCentroidX() const {
  if (nodeSequence.size() <= 2) return 0.0;
  double sumX = 0.0; int count = 0;
  for (int nid : nodeSequence) {
    // [OPT-A] getNodeRaw + getNodeType
    if (instance->getNodeType(nid) == NodeType::CUSTOMER) {
      sumX += instance->getNodeRaw(nid)->getX(); ++count;
    }
  }
  return count > 0 ? sumX / count : 0.0;
}

double Route::getCentroidY() const {
  if (nodeSequence.size() <= 2) return 0.0;
  double sumY = 0.0; int count = 0;
  for (int nid : nodeSequence) {
    // [OPT-A] getNodeRaw + getNodeType
    if (instance->getNodeType(nid) == NodeType::CUSTOMER) {
      sumY += instance->getNodeRaw(nid)->getY(); ++count;
    }
  }
  return count > 0 ? sumY / count : 0.0;
}

size_t Route::size() const { return nodeSequence.size(); }

// =============================================================================
// Debug / print
// =============================================================================
void Route::print() const {
  evaluate();
  std::cout << "Route Id:" << id
            << " - Total Distance: " << evalResult.totalDistance
            << " - Total Time: "     << evalResult.totalTime
            << " - Total Charge Amount: " << evalResult.totalChargeAmount
            << std::endl;
  for (size_t i = 0; i < nodeSequence.size() - 1; ++i)
    std::cout << nodeSequence[i] << "->";
  std::cout << nodeSequence.back() << std::endl;
}

std::string Route::toString() const {
  evaluate();
  runAbsorptionIfNeeded();
  std::stringstream ss;
  ss << std::fixed << std::setprecision(2);
  ss << "--- Route ID: " << id << " ---"
     << " Feasible: " << (evalResult.feasible ? "YES" : "NO") << "\n"
     << "   Total Distance:    " << std::setw(8) << evalResult.totalDistance << "\n"
     << "   Total Time:        " << std::setw(8) << evalResult.totalTime << "\n"
     << "   Total Energy Cons: " << std::setw(8) << evalResult.totalEnergyConsumption << "\n";

  if (nodeSequence.empty())         { ss << "   [Route is Empty]\n"; return ss.str(); }
  if (nodeSequence.size() != states.size()) {
    ss << "   [ERROR: size mismatch nodeSeq=" << nodeSequence.size()
       << " states=" << states.size() << "]\n";
    return ss.str();
  }

  ss << "--- Node Sequence (Count: " << nodeSequence.size() << ") ---\n";
  ss << std::setw(5) << "Idx"   << " | " << std::setw(5)  << "StrID" << " | "
     << std::setw(10) << "Type" << " | " << std::setw(8)  << "ArrTime" << " | "
     << std::setw(8) << "DepTime" << " | " << std::setw(8) << "RemBat" << " | "
     << std::setw(8) << "RemLoad" << " | " << std::setw(8) << "Charge" << "\n";
  ss << std::string(87, '-') << "\n";

  for (size_t i = 0; i < nodeSequence.size(); ++i) {
    const Node* node = instance->getNodeRaw(nodeSequence[i]);
    const NodeState &st = states[i];
    const char *typeStr =
        node->getType() == NodeType::CUSTOMER ? "Customer" :
        node->getType() == NodeType::STATION  ? "Station"  : "Depot";
    ss << std::setw(5)  << node->getId()       << " | "
       << std::setw(5)  << node->getStringId() << " | "
       << std::setw(10) << typeStr             << " | "
       << std::setw(8)  << st.arrivalTime      << " | "
       << std::setw(8)  << st.departureTime    << " | "
       << std::setw(8)  << st.remainingBattery << " | "
       << std::setw(8)  << st.remainingLoad    << " | "
       << std::setw(8)  << st.chargeAmount     << "\n";
  }
  ss << "\n";
  return ss.str();
}

long long Route::getHash() const {
  evaluate();
  long long hash = 0;
  for (int nid : nodeSequence) hash = hash * 31 + nid;
  return hash;
}

// =============================================================================
// canPossiblyInsert — Tier 3 bounding check (O(1), no simulation)
// =============================================================================
bool Route::canPossiblyInsert(int nodeIdToInsert, size_t position,
                              int nodeIdToRemove) const {
  evaluate();
  if (position < 1 || position > nodeSequence.size() - 1) return false;
  if (!evalResult.feasible) return false;

  const double EPSILON   = 1e-9;
  const auto  &prevState = states[position - 1];
  int prev_id            = nodeSequence[position - 1];
  // [OPT-A] getNodeRaw + static_cast
  const Node* nodeToInsert = instance->getNodeRaw(nodeIdToInsert);

  double avail = prevState.remainingLoad;
  if (nodeIdToRemove != -1) {
    const Node* nr = instance->getNodeRaw(nodeIdToRemove);
    if (nr->getType() == NodeType::CUSTOMER) avail += nr->getDemand();
  }
  if (nodeToInsert->getDemand() > avail + EPSILON) return false;

  double energy = instance->getDistance(prev_id, nodeIdToInsert) * vehicle->getEnergyConsumptionRate();
  if (prevState.remainingBattery < energy - EPSILON) return false;

  double arrival = prevState.departureTime + instance->getTime(prev_id, nodeIdToInsert);
  if (arrival > nodeToInsert->getDueDate() + EPSILON) return false;

  return true;
}

// =============================================================================
// fastForwardCheck — Tier 2 approximate check (single-node insertion)
// =============================================================================
InsertionResult Route::fastForwardCheck(int nodeId, size_t position) const {
  evaluate();
  const double EPSILON = 1e-9;
  const double batCap  = vehicle->getBatteryCapacity();
  const double eRate   = vehicle->getEnergyConsumptionRate();

  int prevId = nodeSequence[position - 1];
  int nextId = nodeSequence[position];

  double oldEdge = instance->getDistance(prevId, nextId);
  double newEdge = instance->getDistance(prevId, nodeId)
                 + instance->getDistance(nodeId, nextId);
  double deltaDist = newEdge - oldEdge;

  const int origN = static_cast<int>(nodeSequence.size());
  const int simN  = origN + 1;

  static thread_local std::vector<double> ffc_dp;
  ffc_dp.assign(simN, 0.0);

  auto simNodeAt = [&](int i) -> int {
    if (i < static_cast<int>(position)) return nodeSequence[i];
    if (i == static_cast<int>(position)) return nodeId;
    return nodeSequence[i - 1];
  };

  for (int i = simN - 2; i >= 0; --i) {
    double e   = instance->getDistance(simNodeAt(i), simNodeAt(i + 1)) * eRate;
    // [OPT-A] getNodeType — flat array lookup
    if (instance->getNodeType(simNodeAt(i + 1)) == NodeType::STATION)
      ffc_dp[i] = e + std::max(0.0, ffc_dp[i + 1] - batCap);
    else
      ffc_dp[i] = e + ffc_dp[i + 1];
    if (ffc_dp[i] > batCap + EPSILON) return {false};
  }

  NodeState cur = states[0];
  double simWait = 0.0;

  for (int i = 0; i < simN - 1; ++i) {
    int from_id  = simNodeAt(i);
    int to_id    = simNodeAt(i + 1);
    // [OPT-A] getNodeRaw + static_cast
    const Node* to_node = instance->getNodeRaw(to_id);
    double dist   = instance->getDistance(from_id, to_id);
    double energy = dist * eRate;
    double arr    = cur.departureTime + instance->getTime(from_id, to_id);
    double bat    = cur.remainingBattery - energy;
    if (bat < -EPSILON) return {false};

    switch (to_node->getType()) {
    case NodeType::CUSTOMER: {
      const Customer* cust = static_cast<const Customer*>(to_node);
      double wait = std::max(0.0, cust->getReadyTime() - arr);
      simWait += wait;
      double svc = arr + wait;
      if (svc > cust->getDueDate() + EPSILON) return {false};
      cur.remainingLoad -= cust->getDemand();
      if (cur.remainingLoad < -EPSILON) return {false};
      cur.departureTime     = svc + cust->getServiceTime();
      cur.remainingBattery  = bat;
      break;
    }
    case NodeType::STATION: {
      const Station* station = static_cast<const Station*>(to_node);
      double wait = std::max(0.0, station->getReadyTime() - arr);
      simWait += wait;
      double cs = arr + wait;
      if (cs > station->getDueDate() + EPSILON) return {false};
      double chargeNeeded   = std::max(0.0, ffc_dp[i + 1] - bat);
      double chargePossible = batCap - bat;
      double chargeAmt      = std::min(chargeNeeded, chargePossible);
      if (chargeAmt < chargeNeeded - EPSILON) return {false};
      double ct = chargeAmt * station->getChargingRate();
      if (cs + ct > station->getDueDate() + EPSILON) return {false};
      cur.departureTime    = cs + ct;
      cur.remainingBattery = bat + chargeAmt;
      break;
    }
    case NodeType::DEPOT: {
      const Depot* depot = static_cast<const Depot*>(to_node);
      if (arr > depot->getDueDate() + EPSILON) return {false};
      cur.departureTime    = arr;
      cur.remainingBattery = bat;
      break;
    }
    }
  }

  InsertionResult res;
  res.isFeasible    = true;
  res.deltaDistance = deltaDist;
  double oldWait = 0.0;
  for (size_t i = position - 1; i < states.size(); ++i)
    oldWait += states[i].timeWait;
  res.deltaWaitTime = simWait - oldWait;
  return res;
}

// =============================================================================
// quickCapacityCheck
// =============================================================================
bool Route::quickCapacityCheck(double demand) const {
  evaluate();
  if (states.empty()) return true;
  return demand <= states.back().remainingLoad;
}

// =============================================================================
// Energy / battery getters
// =============================================================================
const std::vector<double> &Route::getMinBatteryReq() const {
  evaluate();
  return minBatteryReq;
}

std::vector<double> Route::getEnergySlack() const {
  evaluate();
  runAbsorptionIfNeeded();
  const int n = static_cast<int>(nodeSequence.size());
  std::vector<double> slack(n, 0.0);
  if (n != static_cast<int>(states.size()) || n != static_cast<int>(minBatteryReq.size()))
    return slack;
  for (int i = 0; i < n; ++i)
    slack[i] = states[i].remainingBattery - minBatteryReq[i];
  return slack;
}

std::vector<double> Route::getTimeSlack() const {
  evaluate();
  const int n = static_cast<int>(nodeSequence.size());
  std::vector<double> slack(n, 0.0);
  if (n != static_cast<int>(states.size())) return slack;

  // [OPT-A] getNodeRaw thay getNodeById
  const Node* lastNode = instance->getNodeRaw(nodeSequence[n - 1]);
  slack[n - 1] = std::max(0.0, lastNode->getDueDate() - states[n - 1].arrivalTime);
  for (int i = n - 2; i >= 0; --i) {
    const Node* node = instance->getNodeRaw(nodeSequence[i]);
    double local = std::max(0.0, node->getDueDate() - states[i].departureTime);
    slack[i] = std::min(local, slack[i + 1] + states[i + 1].timeWait);
  }
  return slack;
}

std::vector<std::pair<int, double>>
Route::getBottleneckNodes(double thresholdRatio) const {
  evaluate();
  runAbsorptionIfNeeded();
  std::vector<std::pair<int, double>> bottlenecks;
  double threshold = thresholdRatio * vehicle->getBatteryCapacity();
  auto slack = getEnergySlack();
  for (int i = 1; i < static_cast<int>(slack.size()) - 1; ++i)
    if (slack[i] < threshold)
      bottlenecks.push_back({i, slack[i]});
  return bottlenecks;
}

// =============================================================================
// Station management
// =============================================================================
std::vector<int> Route::getRedundantStations() const {
  evaluate();
  runAbsorptionIfNeeded(); // battery state affects bypass feasibility
  std::vector<int> redundant;
  const int n = static_cast<int>(nodeSequence.size());
  if (n != static_cast<int>(states.size()) ||
      n != static_cast<int>(minBatteryReq.size()) ||
      !evalResult.feasible)
    return redundant;

  const double EPSILON = 1e-9;
  for (int i = 1; i < n - 1; ++i) {
    // [OPT-A] getNodeType — flat array lookup
    if (instance->getNodeType(nodeSequence[i]) != NodeType::STATION)
      continue;
    int    prevId       = nodeSequence[i - 1];
    int    nextId       = nodeSequence[i + 1];
    double energyBypass = instance->getDistance(prevId, nextId)
                        * vehicle->getEnergyConsumptionRate();
    double batAtPrev    = states[i - 1].remainingBattery;
    double dpNext       = minBatteryReq[i + 1];
    if (batAtPrev >= energyBypass + dpNext - EPSILON)
      redundant.push_back(i);
  }
  return redundant;
}

void Route::pruneRedundantStations() {
  bool changed = true;
  while (changed) {
    changed = false;
    evaluate();
    runAbsorptionIfNeeded();
    auto redundant = getRedundantStations();
    if (!redundant.empty()) {
      for (int i = static_cast<int>(redundant.size()) - 1; i >= 0; --i)
        removeNode(redundant[i]);
      changed = true;
    }
  }
}