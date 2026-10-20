#pragma once
#include "core/Solution.h"
#include "core/Customer.h"
#include "core/Station.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>

namespace audit {
inline void require(bool value, const std::string& message) {
  if (!value) throw std::runtime_error(message);
}
inline void near(double actual, double expected, const std::string& message) {
  require(std::isfinite(actual) && std::isfinite(expected) &&
          std::abs(actual-expected) <= 1e-7 * std::max({1.0,std::abs(actual),std::abs(expected)}),
          message + " (actual=" + std::to_string(actual) + ", expected=" + std::to_string(expected) + ")");
}
struct Metrics { double distance=0, duration=0, wait=0, charge=0, chargeTime=0, active=0; };

// Replay the returned schedule, not the evaluator's feasibility decision or
// its backward battery requirements. This also checks conservation after charging.
inline Metrics validateRoute(const Route& route, const Instance& inst) {
  require(route.isFeasible(), "route reported infeasible");
  const auto& nodes=route.getNodes();
  const auto& states=route.getStates();
  require(nodes.size()>=2 && nodes.front()==0 && nodes.back()==0, "depot endpoints");
  require(states.size()==nodes.size(), "state count");
  const auto vehicle=route.getVehicle();
  double battery=vehicle->getBatteryCapacity(), load=vehicle->getCapacity();
  double clock=inst.getNodeRaw(0)->getReadyTime();
  near(states[0].departureTime,clock,"depot departure");
  near(states[0].remainingBattery,battery,"initial battery");
  Metrics result;
  for(size_t i=1;i<nodes.size();++i) {
    const Node* node=inst.getNodeRaw(nodes[i]);
    // Read the declared customer data independently of Node's base fields.
    const auto* customer=node->getType()==NodeType::CUSTOMER?static_cast<const Customer*>(node):nullptr;
    const double ready=customer?customer->getReadyTime():node->getReadyTime();
    const double due=customer?customer->getDueDate():node->getDueDate();
    const auto& state=states[i];
    const double distance=inst.getDistance(nodes[i-1],nodes[i]);
    result.distance+=distance;
    clock+=inst.getTime(nodes[i-1],nodes[i]);
    battery-=distance*vehicle->getEnergyConsumptionRate();
    near(state.arrivalTime,clock,"arrival time at position "+std::to_string(i));
    require(battery>=-1e-7,"negative arrival battery");
    double wait=0;
    if(node->getType()!=NodeType::DEPOT) {
      wait=std::max(0.0,ready-clock);
      clock+=wait;
    }
    require(clock<=due+1e-7,"time window at node "+std::to_string(nodes[i]));
    near(state.timeWait,wait,"waiting time");
    result.wait+=wait;
    if(node->getType()==NodeType::STATION) {
      const auto* station=static_cast<const Station*>(node);
      require(state.chargeAmount>=-1e-7,"negative charge");
      battery+=state.chargeAmount;
      require(battery<=vehicle->getBatteryCapacity()+1e-7,"battery over capacity");
      const double chargingTime=state.chargeAmount*station->getChargingRate();
      clock+=chargingTime;
      require(clock<=node->getDueDate()+1e-7,"station closing");
      result.charge+=state.chargeAmount;
      result.chargeTime+=chargingTime;
    } else {
      near(state.chargeAmount,0,"charge outside station");
      if(node->getType()==NodeType::CUSTOMER) {
        load-=node->getDemand();
        require(load>=-1e-7,"capacity");
        clock+=customer->getServiceTime();
      } else require(i+1==nodes.size(),"internal depot");
    }
    near(state.departureTime,clock,"departure time");
    near(state.remainingBattery,battery,"remaining battery");
    near(state.remainingLoad,load,"remaining load");
  }
  result.duration=clock-inst.getNodeRaw(0)->getReadyTime();
  result.active=result.duration-result.wait;
  near(route.getTotalDistance(),result.distance,"distance total");
  near(route.getTotalTime(),result.duration,"duration total");
  near(route.getTotalWaitTime(),result.wait,"wait total");
  near(route.getTotalChargeAmount(),result.charge,"charge total");
  near(route.getTotalChargeTime(),result.chargeTime,"charge time total");
  near(route.getTotalEnergyConsumption(),result.distance*vehicle->getEnergyConsumptionRate(),"energy total");
  near(route.getActiveTime(),result.active,"active time");
  return result;
}
inline void validateSolution(const Solution& solution, const Instance& inst) {
  std::unordered_map<int,int> seen;
  std::vector<double> active;
  double distance=0,maxTime=0;
  for(const auto& route:solution.getRoutes()) {
    const auto metrics=validateRoute(route,inst);
    active.push_back(metrics.active);
    distance+=metrics.distance;
    maxTime=std::max(maxTime,metrics.duration);
    for(int id:route.getNodes())
      if(inst.getNodeType(id)==NodeType::CUSTOMER) ++seen[id];
  }
  for(const auto& customer:inst.getCustomers()) require(seen[customer->getId()]==1,"customer coverage");
  near(solution.getTotalDistance(),distance,"solution distance");
  near(solution.getMaxTime(),maxTime,"solution max time");
  near(solution.getTotalVehicles(),active.size(),"solution vehicles");
  double sum=0,pairwise=0;
  for(double a:active) { sum+=a; for(double b:active) pairwise+=std::abs(a-b); }
  const double gini=sum>1e-6?pairwise/(2*active.size()*sum):0;
  near(solution.getWorkloadGini(),gini,"solution Gini");
}
} // namespace audit
