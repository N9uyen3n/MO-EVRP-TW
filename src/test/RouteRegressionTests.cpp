#include "ScheduleValidator.h"
#include "core/Customer.h"
#include "core/Depot.h"
#include <functional>
#include <iostream>
#include <random>

using audit::require;
using audit::near;
using Nodes=std::vector<std::shared_ptr<Node>>;
static std::shared_ptr<Depot> depot(double ready=0) { return std::make_shared<Depot>(0,"D",0,0,ready,1000); }
static std::shared_ptr<Station> station(int id,double x,double rate=1,double due=1000) {
  return std::make_shared<Station>(id,"S",x,0,0,due,rate);
}
static std::shared_ptr<Customer> customer(int id,double x,double ready=0,double due=1000,double demand=1,double service=0) {
  return std::make_shared<Customer>(id,"C",x,0,demand,ready,due,service);
}
static Route route(const std::shared_ptr<Instance>& inst,std::initializer_list<int> ids) {
  Route result(0,std::make_shared<Vehicle>(0,inst->getVehicleCapacity(),inst->getVehicleBattery(),inst->getVehicleEnergyRate()),inst);
  for(int id:ids) result.addNode(id);
  result.evaluate();
  return result;
}
int main() {
  int failures=0,passed=0;
  const auto test=[&](const std::string& name,const std::function<void()>& body) {
    try { body(); ++passed; std::cout<<"PASS "<<name<<'\n'; }
    catch(const std::exception& error) { ++failures; std::cout<<"FAIL "<<name<<": "<<error.what()<<'\n'; }
  };
  test("solution evaluation removes every route without customers",[] {
    auto inst=std::make_shared<Instance>(Nodes{depot(),station(1,1),customer(2,3)},100,100,1,1);
    Solution sol(inst);
    sol.addRoute(route(inst,{2}));
    sol.evaluateRoutes();
    const double distance=sol.getTotalDistance(),gini=sol.getWorkloadGini(),maxTime=sol.getMaxTime();
    Solution original=sol;
    sol.addRoute(route(inst,{}));
    sol.addRoute(route(inst,{1}));
    require(!sol.isFeasible(),"unprocessed customer-free routes must not be feasible");
    bool rejected=false;
    try { audit::validateSolution(sol,*inst); }
    catch(const std::exception&) { rejected=true; }
    require(rejected,"solution checker must reject customer-free routes");
    sol.evaluateRoutes();
    require(sol.getNumRoutes()==1 && sol.getTotalVehicles()==1,"only customer-serving route remains");
    near(sol.getTotalDistance(),distance,"station-only distance excluded");
    near(sol.getWorkloadGini(),gini,"station-only workload excluded");
    near(sol.getMaxTime(),maxTime,"station-only duration excluded");
    require(sol.isFeasible(),"customer-serving solution remains feasible");
    audit::validateSolution(sol,*inst);
    require(original.getNumRoutes()==1,"copy-on-write preserves original");
    Solution unused(inst);
    unused.addRoute(route(inst,{1})); unused.evaluateRoutes();
    require(unused.getNumRoutes()==0 && unused.getTotalVehicles()==0,"unused fleet is removed");
    require(!unused.isFeasible(),"removing unused routes must not conceal missing customers");
  });
  test("customer time data agrees through Node API",[] {
    auto c=customer(1,5,10,80,1,7); const Node* node=c.get();
    near(node->getReadyTime(),10,"base ready time");
    near(node->getDueDate(),80,"base due date");
    near(node->getServiceTime(),7,"base service time");
  });
  test("absorption respects intermediate deadline",[] {
    auto inst=std::make_shared<Instance>(Nodes{depot(),station(1,10),customer(2,11,0,12),customer(3,12,30,100)},100,100,1,1);
    auto r=route(inst,{1,2,3}); audit::validateRoute(r,*inst);
  });
  test("getters do not change active time or Gini",[] {
    auto inst=std::make_shared<Instance>(Nodes{depot(),station(1,10),customer(2,11,0,12),customer(3,12,30,100),customer(4,2)},100,100,1,1);
    auto r=route(inst,{1,2,3}); const double before=r.getActiveTime();
    Solution sol(inst); sol.addRoute(r); sol.addRoute(route(inst,{4})); sol.evaluateRoutes();
    const double gini=sol.getWorkloadGini();
    r.getStates(); r.getTotalChargeAmount(); r.toString();
    near(r.getActiveTime(),before,"active time changed after getter");
    for(const auto& item:sol.getRoutes()) item.getStates();
    sol.evaluateRoutes(); near(sol.getWorkloadGini(),gini,"Gini changed after getter");
    audit::validateSolution(sol,*inst);
  });
  test("extra charge propagates to depot and later stations",[] {
    auto inst=std::make_shared<Instance>(Nodes{depot(),station(1,10),customer(2,11),customer(3,12,30),station(4,15,2),customer(5,17,60)},100,30,1,1);
    audit::validateRoute(route(inst,{1,2,3}),*inst);
    audit::validateRoute(route(inst,{1,2,3,4,5}),*inst);
  });
  test("safe filter allows upstream recharge",[] {
    auto inst=std::make_shared<Instance>(Nodes{depot(),station(1,10),customer(2,11),station(3,12),customer(4,21)},100,20,1,1);
    auto r=route(inst,{1,2,3});
    require(r.checkInsertionCost(4,3).isFeasible,"exact insertion should be feasible");
    require(r.canPossiblyInsert(4,3),"old battery rejects possible recharge");
    r.addNode(4,3); audit::validateRoute(r,*inst);
  });
  test("global capacity and removal hints",[] {
    auto inst=std::make_shared<Instance>(Nodes{depot(),customer(1,1,0,1000,6),customer(2,2,0,1000,6)},10,100,1,1);
    auto r=route(inst,{1});
    require(!r.canPossiblyInsert(2,2),"overload must be rejected");
    require(r.canPossiblyInsert(1,1,1),"intra move should reclaim removed demand");
    require(!r.checkInsertionCost(2,2).isFeasible,"exact capacity");
  });
  test("nonzero depot ready and station closing",[] {
    auto inst=std::make_shared<Instance>(Nodes{depot(7),station(1,10,1,19),customer(2,11,0,21),customer(3,12,40)},100,30,1,1);
    auto r=route(inst,{1,2,3}); audit::validateRoute(r,*inst);
    near(r.getStates().front().departureTime,7,"depot opens at seven");
    require(r.getStates()[1].departureTime<=19+1e-8,"charging past station closing");
  });
  test("duplicate consecutive station and empty route",[] {
    auto inst=std::make_shared<Instance>(Nodes{depot(),station(1,1)},100,100,1,1);
    auto empty=route(inst,{}); audit::validateRoute(empty,*inst);
    auto r=route(inst,{1});
    require(!r.checkInsertionCost(1,2).isFeasible,"duplicate station insertion");
    r.addNode(1); require(!r.isFeasible(),"duplicate station route");
  });
  test("upstream waiting can avoid slow downstream charging",[] {
    auto inst=std::make_shared<Instance>(Nodes{depot(),station(1,10,.5),customer(2,11,30),station(3,12,2,35)},100,20,1,1);
    auto r=route(inst,{1,2,3}); audit::validateRoute(r,*inst);
    require(r.getStates()[1].chargeAmount>0,"upstream spare waiting should charge");
    require(r.getStates()[3].departureTime<=35+1e-8,"slow station must close on time");
  });
  test("zero-rate station and cache invalidation",[] {
    auto inst=std::make_shared<Instance>(Nodes{depot(),station(1,10,0),customer(2,12,30)},100,20,1,1);
    auto r=route(inst,{1,2}); audit::validateRoute(r,*inst);
    const double original=r.getTotalDistance();
    r.removeNode(2); audit::validateRoute(r,*inst);
    r.addNode(2); audit::validateRoute(r,*inst);
    near(r.getTotalDistance(),original,"mutation invalidates schedule");
  });
  test("station pruning rechecks dependencies after each removal",[] {
    auto inst=std::make_shared<Instance>(Nodes{depot(),station(1,5),customer(2,10,20),station(3,11),customer(4,12)},100,20,1,1);
    auto r=route(inst,{1,2,3,4}); audit::validateRoute(r,*inst);
    r.pruneRedundantStations(); audit::validateRoute(r,*inst);
  });
  test("insertion delta and filter differential on directed matrices",[] {
    std::mt19937 rng(20261020);
    for(int iteration=0;iteration<100;++iteration) {
      Nodes nodes{depot(),station(1,4,0.5),customer(2,6),station(3,8,2),customer(4,10,20),customer(5,12,40)};
      auto inst=std::make_shared<Instance>(nodes,10,30,1,1);
      const int n=nodes.size(); std::vector<double> distances(n*n),times(n*n);
      for(int i=0;i<n;++i) for(int j=0;j<n;++j) if(i!=j) {
        distances[i*n+j]=1+rng()%9; times[i*n+j]=1+rng()%7;
      }
      inst->setTravelMatrices(distances,times);
      auto base=route(inst,{1,2,3,4});
      if(!base.isFeasible()) continue;
      audit::validateRoute(base,*inst);
      for(size_t pos=1;pos<base.size();++pos) {
        const auto delta=base.checkInsertionCost(5,pos);
        auto actual=base; actual.addNode(5,pos); actual.evaluate();
        require(delta.isFeasible==actual.isFeasible(),"insertion/full feasibility");
        if(!actual.isFeasible()) continue;
        require(base.canPossiblyInsert(5,pos),"safe filter false negative");
        audit::validateRoute(actual,*inst);
        near(delta.deltaDistance,actual.getTotalDistance()-base.getTotalDistance(),"distance delta");
        near(delta.deltaTime,actual.getTotalTime()-base.getTotalTime(),"time delta");
        near(delta.deltaWaitTime,actual.getTotalWaitTime()-base.getTotalWaitTime(),"wait delta");
        near(delta.deltaChargeAmount,actual.getTotalChargeAmount()-base.getTotalChargeAmount(),"charge delta");
      }
    }
  });
  std::cout<<"RESULT passed="<<passed<<" failed="<<failures<<'\n';
  return failures?1:0;
}
