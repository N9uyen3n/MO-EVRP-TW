// Route scheduling and virtual insertion share one canonical O(n) evaluator.
// All getters observe the same completed charging policy for a node sequence.

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
  cachedDemandDirty_    = true;
  cachedCustomersDirty_ = true;
}

void Route::addNode(int nodeId) {
  nodeSequence.insert(nodeSequence.end() - 1, nodeId);
  isDirty          = true;
  cachedDemandDirty_    = true;
  cachedCustomersDirty_ = true;
}

void Route::removeNode(size_t position) {
  if (position < 1 || position > nodeSequence.size() - 2) {
    std::cerr << "[DEBUG removeNode] pos=" << position
              << " size=" << nodeSequence.size()
              << " seq=[";
    for (size_t i = 0; i < nodeSequence.size(); ++i)
      std::cerr << nodeSequence[i] << (i+1<nodeSequence.size()?",":"");
    std::cerr << "]\n";
    throw std::out_of_range("Vi tri xoa node khong hop le. Khong the xoa depot.");
  }
  nodeSequence.erase(nodeSequence.begin() + position);
  isDirty          = true;
  cachedDemandDirty_    = true;
  cachedCustomersDirty_ = true;
}

void Route::clear() {
  nodeSequence.clear();
  nodeSequence.push_back(0);
  nodeSequence.push_back(0);
  isDirty          = true;
  cachedDemandDirty_    = true;
  cachedCustomersDirty_ = true;
  evaluate();
}

void Route::reverseNodes(size_t i, size_t j) {
  if (i < 1 || j >= nodeSequence.size() - 1 || i >= j)
    throw std::out_of_range("Chi so 2-Opt khong hop le.");
  std::reverse(nodeSequence.begin() + i, nodeSequence.begin() + j + 1);
  isDirty          = true;
  // demand + customers unchanged by reversal — no cache invalidation needed
}

namespace {
// Shared by full evaluation and virtual insertion. Charging is a deterministic
// policy: enough to reach the next station/depot, plus charge that fits entirely
// in downstream waiting without violating any intervening time window.
// Each customer belongs to one station lookahead, so evaluation remains O(n).
template<class NodeAt>
EvaluationResult evaluateSequence(const Instance& instance, const Vehicle& vehicle,
                                  int n, NodeAt nodeAt,
                                  std::vector<NodeState>& states,
                                  std::vector<double>& required) {
  constexpr double eps=1e-9;
  EvaluationResult result;
  result.feasible=false;
  states.assign(n,NodeState{});
  required.assign(n,0.0);
  const double capacity=vehicle.getBatteryCapacity();
  const double rate=vehicle.getEnergyConsumptionRate();
  if(n<2 || nodeAt(0)!=0 || nodeAt(n-1)!=0) return result;
  for(int i=n-2;i>=0;--i) {
    required[i]=instance.getDistance(nodeAt(i),nodeAt(i+1))*rate;
    if(instance.getNodeType(nodeAt(i+1))!=NodeType::STATION)
      required[i]+=required[i+1];
    if(required[i]>capacity+eps) return result;
  }
  const Node* depot=instance.getNodeRaw(0);
  states[0].arrivalTime=states[0].departureTime=depot->getReadyTime();
  states[0].remainingBattery=capacity;
  states[0].remainingLoad=vehicle.getCapacity();
  for(int i=1;i<n;++i) {
    const int previous=nodeAt(i-1),id=nodeAt(i);
    const Node* node=instance.getNodeRaw(id);
    const auto& from=states[i-1];
    auto& to=states[i];
    const double distance=instance.getDistance(previous,id);
    result.totalDistance+=distance;
    result.totalEnergyConsumption+=distance*rate;
    to.arrivalTime=from.departureTime+instance.getTime(previous,id);
    to.remainingBattery=from.remainingBattery-distance*rate;
    to.remainingLoad=from.remainingLoad;
    if(to.remainingBattery < -eps) return result;
    if(node->getType()==NodeType::DEPOT) {
      if(i!=n-1 || to.arrivalTime>node->getDueDate()+eps) return result;
      to.departureTime=to.arrivalTime;
      continue;
    }
    to.timeWait=std::max(0.0,node->getReadyTime()-to.arrivalTime);
    result.totalWaitTime+=to.timeWait;
    const double start=to.arrivalTime+to.timeWait;
    if(start>node->getDueDate()+eps) return result;
    if(node->getType()==NodeType::CUSTOMER) {
      to.remainingLoad-=node->getDemand();
      if(to.remainingLoad < -eps) return result;
      to.departureTime=start+node->getServiceTime();
      continue;
    }
    if(previous==id) return result;
    const auto* station=static_cast<const Station*>(node);
    const double chargingRate=station->getChargingRate();
    if(!std::isfinite(chargingRate) || chargingRate<0.0) return result;
    double charge=std::max(0.0,required[i]-to.remainingBattery);
    if(charge>capacity-to.remainingBattery+eps) return result;
    const double minimumDeparture=start+charge*chargingRate;
    if(minimumDeparture>station->getDueDate()+eps) return result;
    if(chargingRate>eps) {
      double extraTime=std::min((capacity-to.remainingBattery-charge)*chargingRate,
                                station->getDueDate()-minimumDeparture);
      double clock=minimumDeparture,cumulativeWait=0.0;
      for(int j=i+1;j<n;++j) {
        const Node* next=instance.getNodeRaw(nodeAt(j));
        clock+=instance.getTime(nodeAt(j-1),nodeAt(j));
        const double wait=next->getType()==NodeType::DEPOT?0.0:
                          std::max(0.0,next->getReadyTime()-clock);
        cumulativeWait+=wait;
        clock+=wait;
        // Delay at this point is max(0, extraTime-cumulativeWait).
        extraTime=std::min(extraTime,cumulativeWait+next->getDueDate()-clock);
        if(next->getType()!=NodeType::CUSTOMER) {
          extraTime=std::min(extraTime,cumulativeWait);
          break;
        }
        clock+=next->getServiceTime();
      }
      charge+=std::max(0.0,extraTime)/chargingRate;
    }
    to.chargeAmount=charge;
    to.remainingBattery+=charge;
    to.departureTime=start+charge*chargingRate;
    result.totalChargeAmount+=charge;
    result.totalChargeTime+=charge*chargingRate;
  }
  result.totalTime=states.back().arrivalTime-states.front().departureTime;
  result.feasible=true;
  return result;
}
} // namespace

void Route::evaluate() const {
  if(!isDirty) return;
  evalResult=evaluateSequence(*instance,*vehicle,static_cast<int>(nodeSequence.size()),
      [&](int i){ return nodeSequence[i]; },states,minBatteryReq);
  isDirty=false;
}

InsertionResult Route::checkInsertionCost(int nodeId,size_t position) const {
  evaluate();
  if(position<1 || position>=nodeSequence.size()) return {};
  static thread_local std::vector<NodeState> scratchStates;
  static thread_local std::vector<double> scratchRequired;
  const auto result=evaluateSequence(*instance,*vehicle,static_cast<int>(nodeSequence.size()+1),
      [&](int i){
        if(i<static_cast<int>(position)) return nodeSequence[i];
        if(i==static_cast<int>(position)) return nodeId;
        return nodeSequence[i-1];
      },scratchStates,scratchRequired);
  if(!result.feasible) return {};
  InsertionResult delta;
  delta.isFeasible=true;
  delta.deltaDistance=result.totalDistance-evalResult.totalDistance;
  delta.deltaTime=result.totalTime-evalResult.totalTime;
  delta.deltaChargeAmount=result.totalChargeAmount-evalResult.totalChargeAmount;
  delta.deltaWaitTime=result.totalWaitTime-evalResult.totalWaitTime;
  delta.deltaEnergyConsumption=result.totalEnergyConsumption-evalResult.totalEnergyConsumption;
  return delta;
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
  if (!cachedDemandDirty_) return cachedTotalDemand_;
  double total = 0.0;
  for (int nid : nodeSequence)
    // [OPT-A] getDemand() trả 0.0 cho non-customer — getNodeRaw đủ dùng
    total += instance->getNodeRaw(nid)->getDemand();
  cachedTotalDemand_ = total;
  cachedDemandDirty_ = false;
  return total;
}

// All state getters read the completed canonical schedule.
const std::vector<NodeState> &Route::getStates() const {
  evaluate();
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
bool Route::canPossiblyInsert(int nodeIdToInsert,size_t position,int nodeIdToRemove) const {
  if(position<1 || position>=nodeSequence.size()) return false;
  constexpr double eps=1e-9;
  double demand=getTotalDemand()+instance->getNodeRaw(nodeIdToInsert)->getDemand();
  if(nodeIdToRemove!=-1) demand-=instance->getNodeRaw(nodeIdToRemove)->getDemand();
  if(demand>vehicle->getCapacity()+eps) return false;
  // A removal can change the predecessor and its schedule; defer to exact evaluation.
  if(nodeIdToRemove!=-1) return true;
  const int previous=nodeSequence[position-1];
  const Node* customer=instance->getNodeRaw(nodeIdToInsert);
  // Use physical bounds, not the old schedule's charge/load/departure state.
  if(instance->getDistance(previous,nodeIdToInsert)*vehicle->getEnergyConsumptionRate()
     >vehicle->getBatteryCapacity()+eps) return false;
  if(instance->getNodeRaw(0)->getReadyTime()+instance->getTime(previous,nodeIdToInsert)
     >customer->getDueDate()+eps) return false;
  return true;
}

// =============================================================================
// fastForwardCheck uses the same policy and deltas as exact insertion.
// =============================================================================
InsertionResult Route::fastForwardCheck(int nodeId,size_t position) const {
  return checkInsertionCost(nodeId,position);
}

// =============================================================================
// quickCapacityCheck
// =============================================================================
bool Route::quickCapacityCheck(double demand) const {
  return getTotalDemand()+demand <= vehicle->getCapacity()+1e-9;
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
    const double constrainedTime=states[i].departureTime-
        (node->getType()==NodeType::CUSTOMER?node->getServiceTime():0.0);
    double local = std::max(0.0, node->getDueDate() - constrainedTime);
    slack[i] = std::min(local, slack[i + 1] + states[i + 1].timeWait);
  }
  return slack;
}

std::vector<std::pair<int, double>>
Route::getBottleneckNodes(double thresholdRatio) const {
  evaluate();
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
  while(true) {
    bool removed=false;
    for(int position:getRedundantStations()) {
      Route trial=*this;
      trial.removeNode(position);
      if(trial.isFeasible()) {
        *this=std::move(trial);
        removed=true;
        break; // Recompute candidates after every change to the charging policy.
      }
    }
    if(!removed) return;
  }
}
