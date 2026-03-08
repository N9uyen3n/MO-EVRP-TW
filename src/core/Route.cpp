#include "../../include/core/Route.h"
#include "../../include/core/Customer.h"
#include "../../include/core/Depot.h"
#include "../../include/core/Instance.h"
#include "../../include/core/Station.h"
#include <algorithm> // Cho std::max
#include <cmath>     // Cho std::abs (kiểm tra số thực)
#include <iomanip>
#include <iostream>
#include <ostream>
#include <sstream> // <-- Cần include thư viện này
#include <stdexcept>

// Thread-local scratchpads to avoid allocation in hot loop
// tl_simNodeSequence removed — Point9 virtual-index eliminates the copy
// entirely
thread_local std::vector<double> tl_dp;
thread_local std::vector<NodeState> tl_simStates;

Route::Route(int id, std::shared_ptr<Vehicle> vehicle,
             const std::shared_ptr<Instance> &instance)
    : id(id), vehicle(vehicle), instance(instance), evalResult() {

  nodeSequence = std::make_shared<std::vector<int>>();
  nodeSequence->push_back(0);
  nodeSequence->push_back(0);

  states = std::make_shared<std::vector<NodeState>>();
  minBatteryReq = std::make_shared<std::vector<double>>();

  evaluate();
}

void Route::detach() const {
  if (nodeSequence.use_count() > 1) {
    nodeSequence = std::make_shared<std::vector<int>>(*nodeSequence);
  }
  // States are always detached in evaluate() before modification
}

void Route::detachStates() const {
  if (states.use_count() > 1) {
    states = std::make_shared<std::vector<NodeState>>(*states);
  }
}

void Route::addNode(int nodeId, size_t position) {
  if (position < 1 || position > nodeSequence->size() - 1) {
    throw std::out_of_range(
        "Vi tri them node khong hop le. Phai nam giua 2 depot.");
  }
  detach();
  nodeSequence->insert(nodeSequence->begin() + position, nodeId);
  isDirty = true;
}

void Route::addNode(int nodeId) {
  detach();
  nodeSequence->insert(nodeSequence->end() - 1, nodeId);
  isDirty = true;
}

void Route::removeNode(size_t position) {
  if (position < 1 || position > nodeSequence->size() - 2) {
    throw std::out_of_range(
        "Vi tri xoa node khong hợp lệ. Khong the xoa depot.");
  }
  detach();
  nodeSequence->erase(nodeSequence->begin() + position);
  isDirty = true;
}

void Route::clear() {
  detach();
  nodeSequence->clear();
  nodeSequence->push_back(0);
  nodeSequence->push_back(0);
  isDirty = true;
  evaluate();
}

void Route::reverseNodes(size_t i, size_t j) {
  if (i < 1 || j >= nodeSequence->size() - 1 || i >= j) {
    throw std::out_of_range("Chi so 2-Opt không hợp lệ.");
  }
  detach();
  std::reverse(nodeSequence->begin() + i, nodeSequence->begin() + j + 1);
  isDirty = true;
}

void Route::evaluate() const {
  if (!isDirty) {
    return;
  }
  detachStates(); // COW for states vector

  int n = nodeSequence->size();
  if (n <= 1) {
    evalResult = EvaluationResult();
    isDirty = false;
    return;
  }

  // ⭐ Fix 1b: Cache references — tránh n lần double-dereference (*nodeSequence)[i]
  const auto& seq = *nodeSequence;
  auto&       st  = *states;

  // ⭐ Fix 1a: Reuse thread_local dp — tránh malloc/memset mỗi lần evaluate()
  tl_dp.assign(n, 0.0);
  auto& dp = tl_dp;

  const double EPSILON = 1e-9;
  const double battCap  = vehicle->getBatteryCapacity();
  const double energyRate = vehicle->getEnergyConsumptionRate();

  // ── Backward Pass: tính min battery requirement tại mỗi position ──
  for (int i = n - 2; i >= 0; --i) {
    double energy_to_next = instance->getDistance(seq[i], seq[i + 1]) * energyRate;
    // ⭐ Fix 1e: raw pointer cast thay shared_ptr cast — không tốn atomic refcount
    const Node* next_node = instance->getNodeById(seq[i + 1]).get();
    if (next_node->getType() == NodeType::STATION) {
      dp[i] = energy_to_next + std::max(0.0, dp[i + 1] - battCap);
    } else {
      dp[i] = energy_to_next + dp[i + 1];
    }
    if (dp[i] > battCap + EPSILON) {
      evalResult = EvaluationResult();
      evalResult.feasible = false;
      isDirty = false;
      return;
    }
  }

  // Store Backward Pass result for station optimization operators
  if (minBatteryReq.use_count() > 1) {
    minBatteryReq = std::make_shared<std::vector<double>>(dp);
  } else {
    *minBatteryReq = dp;
  }

  // ⭐ Fix 1c: Skip clear()+resize() khi size không đổi — tránh memset O(n)
  // Forward pass ghi đè tất cả fields nên không cần zero-initialize
  if ((int)st.size() != n) {
    st.resize(n);
  }
  evalResult = EvaluationResult();

  // ⭐ Fix 1e: raw pointer cho depot — không tạo shared_ptr mới
  const Depot* start_depot = static_cast<const Depot*>(instance->getNodeById(0).get());
  st[0].arrivalTime      = start_depot->getReadyTime();
  st[0].departureTime    = start_depot->getReadyTime();
  st[0].remainingBattery = battCap;
  st[0].remainingLoad    = vehicle->getCapacity();
  st[0].chargeAmount     = 0.0;

  // ── Forward Pass: tính states (time, battery, load) ──
  for (int i = 0; i < n - 1; ++i) {
    int from_id = seq[i];
    int to_id   = seq[i + 1];

    // ⭐ Fix 1e: giữ shared_ptr ref (không copy) rồi lấy raw pointer để cast
    const auto& to_node_ptr = instance->getNodeById(to_id);
    const Node* to_node     = to_node_ptr.get();

    NodeState& from_state = st[i];
    NodeState& to_state   = st[i + 1];

    double distance       = instance->getDistance(from_id, to_id);
    double travel_time    = instance->getTime(from_id, to_id);
    double energy_consumed = distance * energyRate;

    evalResult.totalDistance          += distance;
    evalResult.totalEnergyConsumption += energy_consumed;

    to_state.arrivalTime      = from_state.departureTime + travel_time;
    to_state.remainingBattery = from_state.remainingBattery - energy_consumed;
    to_state.remainingLoad    = from_state.remainingLoad;
    to_state.chargeAmount     = 0.0;

    if (to_state.remainingBattery < -EPSILON) {
      evalResult.feasible = false;
      isDirty = false;
      return;
    }

    switch (to_node->getType()) {
    case NodeType::CUSTOMER: {
      // ⭐ Fix 1e: raw pointer cast — zero cost, không đụng refcount
      const Customer* customer = static_cast<const Customer*>(to_node);
      to_state.remainingLoad -= customer->getDemand();
      if (to_state.remainingLoad < -EPSILON) {
        evalResult.feasible = false;
        isDirty = false;
        return;
      }
      double wait_time = std::max(0.0, customer->getReadyTime() - to_state.arrivalTime);
      to_state.timeWait = wait_time;
      evalResult.totalWaitTime += wait_time;
      double service_start_time = to_state.arrivalTime + wait_time;
      if (service_start_time > customer->getDueDate() + EPSILON) {
        evalResult.feasible = false;
        isDirty = false;
        return;
      }
      st[i + 1].departureTime = service_start_time + customer->getServiceTime();
      break;
    }
    case NodeType::STATION: {
      // ⭐ Fix 1d: inline duplicate station check — xóa loop O(n) thứ 3 bên dưới
      if (i > 0 && seq[i + 1] == seq[i]) {
        evalResult.feasible = false;
        isDirty = false;
        return;
      }
      const Station* station = static_cast<const Station*>(to_node);
      double wait_time = std::max(0.0, station->getReadyTime() - to_state.arrivalTime);
      to_state.timeWait = wait_time;
      evalResult.totalWaitTime += wait_time;
      double charge_start_time = to_state.arrivalTime + wait_time;
      if (charge_start_time > station->getDueDate() + EPSILON) {
        evalResult.feasible = false;
        isDirty = false;
        return;
      }
      double charge_needed  = std::max(0.0, dp[i + 1] - to_state.remainingBattery);
      double charge_possible = battCap - to_state.remainingBattery;
      double charge_amount  = std::min(charge_needed, charge_possible);
      double charge_time    = charge_amount * station->getChargingRate();
      st[i + 1].chargeAmount = charge_amount;
      evalResult.totalChargeTime   += charge_time;
      evalResult.totalChargeAmount += charge_amount;
      st[i + 1].departureTime    = charge_start_time + charge_time;
      st[i + 1].remainingBattery += charge_amount;
      if (st[i + 1].departureTime > station->getDueDate() + EPSILON) {
        evalResult.feasible = false;
        isDirty = false;
        return;
      }
      break;
    }
    case NodeType::DEPOT: {
      const Depot* depot = static_cast<const Depot*>(to_node);
      if (to_state.arrivalTime > depot->getDueDate() + EPSILON) {
        evalResult.feasible = false;
        isDirty = false;
        return;
      }
      st[i + 1].departureTime = to_state.arrivalTime;
      // ⭐ Fix Point7: Removed depot->setLastTime() — global mutation trên shared Depot object
      // gây stale state khi evaluate() nhiều routes trong ALNS (route sau ghi đè route trước).
      // arrivalTime đã được lưu trong st[i+1].arrivalTime — dùng states thay vì depot global state.
      break;
    }
    }
  }

  // ⭐ Fix 1d: Loop duplicate station check đã được inline vào forward pass — xóa loop này

  evalResult.feasible = true;
  evalResult.totalTime = st.back().arrivalTime - st.front().departureTime;
  isDirty = false;
}

// Thread-local scratchpads to avoid allocation in hot loop
// tl_simNodeSequence removed — Point9 virtual-index eliminates the copy entirely
// thread_local std::vector<double> tl_dp;
// thread_local std::vector<NodeState> tl_simStates;

// Tier 1 - Exact Check
//
// ⭐ Point 9: Virtual index — KHÔNG copy nodeSequence. Zero allocation, zero memcpy.
//   simSeq(i) map index trong sim-route [0..n) → node id:
//     i < pos  → origSeq[i]
//     i == pos → nodeId  (inserted node)
//     i > pos  → origSeq[i-1]
//
// ⭐ Point 5: Incremental forward pass — reuse states[0..pos-1] đã tính sẵn.
//   Backward pass vẫn full O(n) vì dp[i] ripple phụ thuộc dp[i+1..n-1].
//   Forward pass bắt đầu từ position → trung bình O(n/2) thay vì O(n).
//   Delta = (sim suffix) - (original suffix) — không cần simulate prefix.
//
InsertionResult Route::checkInsertionCost(int nodeId, size_t position) const {
  evaluate(); // no-op nếu !isDirty — đảm bảo states[] và evalResult valid

  const auto& origSeq  = *nodeSequence;
  const int   origN    = (int)origSeq.size();
  const int   n        = origN + 1;
  const int   pos      = (int)position;

  const double EPSILON    = 1e-9;
  const double battCap    = vehicle->getBatteryCapacity();
  const double energyRate = vehicle->getEnergyConsumptionRate();

  // ⭐ Point 9: virtual index — không alloc không memcpy
  auto simSeq = [&](int i) -> int {
    if (i < pos)  return origSeq[i];
    if (i == pos) return nodeId;
    return origSeq[i - 1];
  };

  // ── Backward Pass: tính dp[] cho sim-route — O(n) không thể tránh ──
  tl_dp.assign(n, 0.0);
  for (int i = n - 2; i >= 0; --i) {
    double energy = instance->getDistance(simSeq(i), simSeq(i + 1)) * energyRate;
    const Node* nxt = instance->getNodeById(simSeq(i + 1)).get();
    tl_dp[i] = (nxt->getType() == NodeType::STATION)
               ? energy + std::max(0.0, tl_dp[i + 1] - battCap)
               : energy + tl_dp[i + 1];
    if (tl_dp[i] > battCap + EPSILON)
      return {false};
  }

  // ── Point 5: Incremental Forward Pass từ position ──
  // states[0..pos-1] giữ nguyên vì không có node/edge nào thay đổi trước pos.
  // Seed "cur" = states[pos-1] (departureTime, remainingBattery, remainingLoad).
  int suffixLen = n - pos;
  if ((int)tl_simStates.size() < suffixLen)
    tl_simStates.resize(suffixLen);

  double sufDistance = 0.0, sufEnergy = 0.0, sufWait = 0.0, sufCharge = 0.0;
  NodeState cur = (*states)[pos - 1]; // seed từ last valid state trước insertion point

  for (int i = pos; i < n; ++i) {
    int from_id = simSeq(i - 1);
    int to_id   = simSeq(i);
    const auto& to_node_ptr = instance->getNodeById(to_id);
    const Node* to_node     = to_node_ptr.get();

    double dist   = instance->getDistance(from_id, to_id);
    double ttime  = instance->getTime(from_id, to_id);
    double energy = dist * energyRate;
    sufDistance += dist;
    sufEnergy   += energy;

    NodeState& to_state = tl_simStates[i - pos];
    to_state.arrivalTime      = cur.departureTime + ttime;
    to_state.remainingBattery = cur.remainingBattery - energy;
    to_state.remainingLoad    = cur.remainingLoad;
    to_state.chargeAmount     = 0.0;

    if (to_state.remainingBattery < -EPSILON)
      return {false};

    switch (to_node->getType()) {
    case NodeType::CUSTOMER: {
      const Customer* cust = static_cast<const Customer*>(to_node);
      to_state.remainingLoad -= cust->getDemand();
      if (to_state.remainingLoad < -EPSILON) return {false};
      double wait = std::max(0.0, cust->getReadyTime() - to_state.arrivalTime);
      sufWait += wait;
      double svc_start = to_state.arrivalTime + wait;
      if (svc_start > cust->getDueDate() + EPSILON) return {false};
      to_state.departureTime = svc_start + cust->getServiceTime();
      break;
    }
    case NodeType::STATION: {
      if (i > 0 && to_id == simSeq(i - 1)) return {false}; // duplicate station
      const Station* station = static_cast<const Station*>(to_node);
      double wait = std::max(0.0, station->getReadyTime() - to_state.arrivalTime);
      sufWait += wait;
      double chrg_start = to_state.arrivalTime + wait;
      if (chrg_start > station->getDueDate() + EPSILON) return {false};
      double chrg_needed   = std::max(0.0, tl_dp[i] - to_state.remainingBattery);
      double chrg_possible = battCap - to_state.remainingBattery;
      double chrg_amount   = std::min(chrg_needed, chrg_possible);
      sufCharge += chrg_amount;
      to_state.chargeAmount     = chrg_amount;
      to_state.departureTime    = chrg_start + chrg_amount * station->getChargingRate();
      to_state.remainingBattery += chrg_amount;
      if (to_state.departureTime > station->getDueDate() + EPSILON) return {false};
      break;
    }
    case NodeType::DEPOT: {
      const Depot* depot = static_cast<const Depot*>(to_node);
      if (to_state.arrivalTime > depot->getDueDate() + EPSILON) return {false};
      to_state.departureTime = to_state.arrivalTime;
      break;
    }
    }
    cur = to_state;
  }

  // ── Tính delta: (sim suffix) - (original suffix) ──
  // Original suffix = edges từ pos-1 đến origN-1, states từ pos đến origN-1
  double origSufDistance = 0.0, origSufEnergy = 0.0, origSufWait = 0.0, origSufCharge = 0.0;
  for (int i = pos - 1; i < origN - 1; ++i) {
    double d = instance->getDistance(origSeq[i], origSeq[i + 1]);
    origSufDistance += d;
    origSufEnergy   += d * energyRate;
  }
  for (int i = pos; i < origN; ++i) {
    origSufWait   += (*states)[i].timeWait;
    origSufCharge += (*states)[i].chargeAmount;
  }

  double simDepotArrival  = tl_simStates[n - 1 - pos].arrivalTime;
  double origDepotArrival = (*states)[origN - 1].arrivalTime;

  InsertionResult res;
  res.isFeasible             = true;
  res.deltaDistance          = sufDistance - origSufDistance;
  res.deltaEnergyConsumption = sufEnergy   - origSufEnergy;
  res.deltaWaitTime          = sufWait     - origSufWait;
  res.deltaChargeAmount      = sufCharge   - origSufCharge;
  res.deltaTime              = simDepotArrival - origDepotArrival;
  return res;
}


// --- GETTERS ---
int Route::getId() const { return id; }

std::shared_ptr<Vehicle> Route::getVehicle() const { return vehicle; }

const std::vector<int> &Route::getNodes() const { return *nodeSequence; }

std::vector<int> Route::getCustomers() const {
  std::vector<int> customers;
  for (int nodeId : *nodeSequence) {
    auto node = instance->getNodeById(nodeId);
    if (node->getType() == NodeType::CUSTOMER) {
      customers.push_back(nodeId);
    }
  }
  return customers;
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
  return evalResult.totalChargeTime;
}

double Route::getTotalChargeAmount() const {
  evaluate();
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
  double totalDemand = 0.0;
  // Node::getDemand() returns 0 for Depot and Station
  for (int nodeId : *nodeSequence) {
    totalDemand += instance->getNodeById(nodeId)->getDemand();
  }
  return totalDemand;
}

const std::vector<NodeState> &Route::getStates() const {
  evaluate();
  return *states;
}

int Route::getNodeAt(size_t pos) const {
  if (pos >= nodeSequence->size())
    return -1;
  return (*nodeSequence)[pos];
}

int Route::getLastNodeId() const {
  if (nodeSequence->size() <= 2) {
    // Route is empty or has only depots, return the starting depot
    return 0;
  }
  // Return the node at the second to last position (before the final depot)
  return (*nodeSequence)[nodeSequence->size() - 2];
}

size_t Route::size() const { return nodeSequence->size(); }

void Route::print() const {
  evaluate();
  std::cout << "Route Id:" << id
            << " - Total Distance: " << evalResult.totalDistance
            << " - Total Time: " << evalResult.totalTime
            << " - Total Charge Amount: " << evalResult.totalChargeAmount
            << std::endl;
  for (size_t i = 0; i < nodeSequence->size() - 1; ++i) {
    std::cout << (*nodeSequence)[i] << "->";
  }
  std::cout << (*nodeSequence)[nodeSequence->size() - 1] << std::endl;
}

std::string Route::toString() const {
  evaluate();
  std::stringstream ss;
  ss << std::fixed << std::setprecision(2);

  ss << "--- Route ID: " << this->id << " ---"
     << " Feasible: " << (this->evalResult.feasible ? "YES" : "NO") << "\n"
     << "   Total Distance:    " << std::setw(8)
     << this->evalResult.totalDistance << "\n"
     << "   Total Time:        " << std::setw(8) << this->evalResult.totalTime
     << "\n"
     << "   Total Energy Cons: " << std::setw(8)
     << this->evalResult.totalEnergyConsumption << "\n";

  if (nodeSequence->empty()) {
    ss << "   [Route is Empty]\n";
    return ss.str();
  }
  if (nodeSequence->size() != states->size()) {
    ss << "   [ERROR: Node sequence and states vectors have different sizes!]\n"
       << "   NodeSequence.size() = " << nodeSequence->size() << "\n"
       << "   States.size() = " << states->size() << "\n";
    return ss.str();
  }

  ss << "--- Node Sequence (Count: " << nodeSequence->size() << ") ---\n";
  ss << std::setw(5) << "Idx" << " | " << std::setw(5) << "StrID" << " | "
     << std::setw(10) << "Type" << " | " << std::setw(8) << "ArrTime" << " | "
     << std::setw(8) << "DepTime" << " | " << std::setw(8) << "RemBat" << " | "
     << std::setw(8) << "RemLoad" << " | " << std::setw(8) << "Charge" << "\n";
  ss << "----------------------------------------------------------------------"
        "-------------\n";

  for (size_t i = 0; i < nodeSequence->size(); ++i) {
    int nodeId = (*nodeSequence)[i];
    const auto &state = (*states)[i];
    const std::shared_ptr<Node> &node = this->instance->getNodeById(nodeId);

    std::string nodeTypeStr = "???";
    switch (node->getType()) {
    case NodeType::CUSTOMER:
      nodeTypeStr = "Customer";
      break;
    case NodeType::STATION:
      nodeTypeStr = "Station";
      break;
    case NodeType::DEPOT:
      nodeTypeStr = "Depot";
      break;
    }

    ss << std::setw(5) << node->getId() << " | " << std::setw(5)
       << node->getStringId() << " | " << std::setw(10) << nodeTypeStr << " | "
       << std::setw(8) << state.arrivalTime << " | " << std::setw(8)
       << state.departureTime << " | " << std::setw(8) << state.remainingBattery
       << " | " << std::setw(8) << state.remainingLoad << " | " << std::setw(8)
       << state.chargeAmount << "\n";
  }
  ss << "\n";

  return ss.str();
}

long long Route::getHash() const {
  evaluate();
  long long hash = 0;
  for (int nodeId : *nodeSequence) {
    hash = hash * 31 + nodeId;
  }
  return hash;
}

// Tier 3 - Bounding Check
bool Route::canPossiblyInsert(int nodeIdToInsert, size_t position,
                              int nodeIdToRemove) const {
  evaluate();

  if (position < 1 || position > nodeSequence->size() - 1) {
    return false;
  }
  if (!evalResult.feasible) {
    return false;
  }

  const auto &prev_state = states->at(position - 1);
  int prev_node_id = (*nodeSequence)[position - 1];
  // ⭐ Fix 1e: raw pointer
  const Node* nodeToInsert = instance->getNodeById(nodeIdToInsert).get();
  const double EPSILON = 1e-9;

  double availableLoad = prev_state.remainingLoad;
  if (nodeIdToRemove != -1) {
    const Node* nodeToRemove = instance->getNodeById(nodeIdToRemove).get();
    if (nodeToRemove->getType() == NodeType::CUSTOMER) {
      availableLoad += nodeToRemove->getDemand();
    }
  }

  if (nodeToInsert->getDemand() > availableLoad + EPSILON) {
    return false;
  }

  double energy_to_node = instance->getDistance(prev_node_id, nodeIdToInsert) *
                          vehicle->getEnergyConsumptionRate();
  if (prev_state.remainingBattery < energy_to_node - EPSILON) {
    return false;
  }

  double travel_time_to_node = instance->getTime(prev_node_id, nodeIdToInsert);
  double arrival_at_node = prev_state.departureTime + travel_time_to_node;
  if (arrival_at_node > nodeToInsert->getDueDate() + EPSILON) {
    return false;
  }

  return true;
}

// Tier 2 - Fast, Approximate Check
InsertionResult Route::fastForwardCheck(int nodeId, size_t position) const {
  evaluate();

  const double EPSILON    = 1e-9;
  const double energyRate = vehicle->getEnergyConsumptionRate();
  const auto&  seq        = *nodeSequence;  // ⭐ Fix 1b: cache reference

  int prevNodeId = seq[position - 1];
  int nextNodeId = seq[position];
  // ⭐ Fix 1e: raw pointer
  const Customer* customerNode =
      static_cast<const Customer*>(instance->getNodeById(nodeId).get());

  double oldEdgeDistance = instance->getDistance(prevNodeId, nextNodeId);
  double newEdgeDistance = instance->getDistance(prevNodeId, nodeId) +
                           instance->getDistance(nodeId, nextNodeId);
  double deltaDistance = newEdgeDistance - oldEdgeDistance;

  NodeState currentState = states->at(position - 1);
  double currentLoad = currentState.remainingLoad;
  double simTotalWaitTime = 0;

  // prev -> customer
  currentState.departureTime    += instance->getTime(prevNodeId, nodeId);
  currentState.remainingBattery -= instance->getDistance(prevNodeId, nodeId) * energyRate;
  currentLoad -= customerNode->getDemand();

  if (currentState.remainingBattery < -EPSILON || currentLoad < -EPSILON ||
      currentState.departureTime > customerNode->getDueDate() + EPSILON) {
    return {false};
  }
  double waitAtCust = std::max(0.0, customerNode->getReadyTime() - currentState.departureTime);
  simTotalWaitTime += waitAtCust;
  currentState.departureTime =
      currentState.departureTime + waitAtCust + customerNode->getServiceTime();

  // customer -> next
  currentState.departureTime    += instance->getTime(nodeId, nextNodeId);
  currentState.remainingBattery -= instance->getDistance(nodeId, nextNodeId) * energyRate;
  if (currentState.remainingBattery < -EPSILON)
    return {false};

  // Ripple simulation
  for (size_t i = position; i < seq.size() - 1; ++i) {
    int current_node_id = seq[i];
    int next_node_id    = seq[i + 1];
    // ⭐ Fix 1e: raw pointer
    const Node* current_node_obj = instance->getNodeById(current_node_id).get();

    if (currentState.departureTime > current_node_obj->getDueDate() + EPSILON)
      return {false};

    double wait_time = std::max(0.0, current_node_obj->getReadyTime() -
                                         currentState.departureTime);
    simTotalWaitTime += wait_time;
    currentState.departureTime += wait_time;

    if (current_node_obj->getType() == NodeType::STATION) {
      double energyToDepot    = instance->getDistance(current_node_id, 0) * energyRate;
      double requiredBattery  = energyToDepot * 1.1;

      if (currentState.remainingBattery < requiredBattery) {
        double chargeAmount = requiredBattery - currentState.remainingBattery;
        chargeAmount = std::min(chargeAmount,
                                vehicle->getBatteryCapacity() - currentState.remainingBattery);
        const Station* station = static_cast<const Station*>(current_node_obj);
        double chargeTime = chargeAmount * station->getChargingRate();
        currentState.departureTime    += chargeTime;
        currentState.remainingBattery += chargeAmount;
      }
    }

    currentState.departureTime    += current_node_obj->getServiceTime();
    currentState.departureTime    += instance->getTime(current_node_id, next_node_id);
    currentState.remainingBattery -= instance->getDistance(current_node_id, next_node_id) * energyRate;
    if (currentState.remainingBattery < -EPSILON)
      return {false};
  }

  const Node* final_depot = instance->getNodeById(seq.back()).get();
  if (currentState.departureTime > final_depot->getDueDate() + EPSILON)
    return {false};

  InsertionResult res;
  res.isFeasible   = true;
  res.deltaDistance = deltaDistance;

  // This is an approximation
  double oldWaitTime = 0;
  for (size_t i = position - 1; i < states->size(); ++i) {
    oldWaitTime += (*states)[i].timeWait;
  }
  res.deltaWaitTime = simTotalWaitTime - oldWaitTime;

  return res;
}

// O(1) pre-filter
bool Route::quickCapacityCheck(double demand) const {
  evaluate();
  if (states->empty()) {
    return true;
  }
  // This is a heuristic. It checks against the final remaining load.
  return demand <= states->back().remainingLoad;
}

const std::vector<double> &Route::getMinBatteryReq() const {
  evaluate();
  return *minBatteryReq;
}

// --- Energy Analysis Methods ---

std::vector<double> Route::getTimeSlack() const {
  evaluate();
  int n = nodeSequence->size();
  std::vector<double> slack(n, 0.0);
  if (n != (int)states->size()) {
    return slack;
  }
  const auto& seq = *nodeSequence;
  for (int i = 0; i < n; ++i) {
    // ⭐ Fix 1e: raw pointer
    const Node* node = instance->getNodeById(seq[i]).get();
    slack[i] = std::max(0.0, node->getDueDate() - (*states)[i].departureTime);
  }
  return slack;
}

std::vector<double> Route::getEnergySlack() const {
  evaluate();
  int n = nodeSequence->size();
  std::vector<double> slack(n, 0.0);
  if (n != (int)states->size() || n != (int)minBatteryReq->size()) {
    return slack; // Safety: mismatched sizes
  }
  for (int i = 0; i < n; ++i) {
    slack[i] = (*states)[i].remainingBattery - (*minBatteryReq)[i];
  }
  return slack;
}

std::vector<std::pair<int, double>>
Route::getBottleneckNodes(double thresholdRatio) const {
  evaluate();
  std::vector<std::pair<int, double>> bottlenecks;
  double threshold = thresholdRatio * vehicle->getBatteryCapacity();
  auto slack = getEnergySlack();
  for (int i = 1; i < (int)slack.size() - 1; ++i) { // Skip depots
    if (slack[i] < threshold) {
      bottlenecks.push_back({i, slack[i]});
    }
  }
  return bottlenecks;
}

std::vector<int> Route::getRedundantStations() const {
  evaluate();
  std::vector<int> redundant;
  int n = nodeSequence->size();
  if (n != (int)states->size() || n != (int)minBatteryReq->size() ||
      !evalResult.feasible) {
    return redundant;
  }
  double capacity = vehicle->getBatteryCapacity();
  const double EPSILON = 1e-9;

  const auto& seq = *nodeSequence;  // ⭐ Fix 1b
  for (int i = 1; i < n - 1; ++i) {
    // ⭐ Fix 1e: raw pointer
    const Node* node = instance->getNodeById(seq[i]).get();
    if (node->getType() != NodeType::STATION)
      continue;

    // Condition 1: Low charge amount (< 10% capacity)
    bool lowCharge = (*states)[i].chargeAmount < 0.10 * capacity;

    // Condition 2: Bypass feasible (can skip station and still complete route)
    bool bypassable = false;
    if (i >= 1 && i + 1 < n) {
      int prevId = seq[i - 1];
      int nextId = seq[i + 1];
      double energyBypass = instance->getDistance(prevId, nextId) *
                            vehicle->getEnergyConsumptionRate();
      double batteryAtPrev = (*states)[i - 1].remainingBattery;
      double minReqAtNext = (*minBatteryReq)[i + 1];
      bypassable = (batteryAtPrev >= energyBypass + minReqAtNext - EPSILON);
    }

    if (lowCharge || bypassable) {
      redundant.push_back(i);
    }
  }
  return redundant;
}