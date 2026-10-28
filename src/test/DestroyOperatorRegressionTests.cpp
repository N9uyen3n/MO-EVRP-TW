#include "ScheduleValidator.h"
#include "alns/operators/destroy/EnergyCriticalRemoval.h"
#include "alns/operators/destroy/RouteMergingDestroy.h"
#include "core/Customer.h"
#include "core/Depot.h"
#include <functional>
#include <iostream>

struct DestroyOperatorRegressionAccess {
  static double risk(const EnergyCriticalRemoval& op, int id, const Route& route) {
    return op.calculateEnergyRiskScore(id, route);
  }
  static bool compatible(const RouteMergingDestroy& op, const Route& a, const Route& b) {
    return op.canPotentiallyMerge(a,b);
  }
};

using Nodes=std::vector<std::shared_ptr<Node>>;
using audit::require;
using audit::near;
static std::shared_ptr<Depot> depot() { return std::make_shared<Depot>(0,"D",0,0,0,1000); }
static std::shared_ptr<Customer> customer(int id,double x,double ready=0,double due=1000,double service=0) {
  return std::make_shared<Customer>(id,"C",x,0,1,ready,due,service);
}
static std::shared_ptr<Station> station(int id,double x) {
  return std::make_shared<Station>(id,"S",x,0,0,1000,1);
}
static Route route(const std::shared_ptr<Instance>& inst,std::initializer_list<int> ids) {
  Route r(0,std::make_shared<Vehicle>(0,inst->getVehicleCapacity(),inst->getVehicleBattery(),inst->getVehicleEnergyRate()),inst);
  for(int id:ids) r.addNode(id);
  r.evaluate();
  audit::validateRoute(r,*inst);
  return r;
}
int main() {
  int passed=0,failed=0;
  const auto test=[&](const char* name,const std::function<void()>& run) {
    try { run(); ++passed; std::cout<<"PASS "<<name<<'\n'; }
    catch(const std::exception& e) { ++failed; std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n'; }
  };
  test("ECR first customer reads its own battery, not depot",[] {
    auto inst=std::make_shared<Instance>(Nodes{depot(),customer(1,4)},100,10,1,1);
    auto r=route(inst,{1}); EnergyCriticalRemoval op(inst,.3);
    near(r.getStates()[1].remainingBattery,6,"customer battery");
    near(DestroyOperatorRegressionAccess::risk(op,1,r),2,"first customer risk");
  });
  test("ECR maps customers through intervening charging stations",[] {
    auto inst=std::make_shared<Instance>(Nodes{depot(),station(1,2),customer(2,5),station(3,6),customer(4,9)},100,12,1,1);
    auto r=route(inst,{1,2,3,4}); EnergyCriticalRemoval op(inst,.3);
    near(r.getStates()[2].remainingBattery,7,"first customer battery");
    near(r.getStates()[4].remainingBattery,9,"second customer battery");
    near(DestroyOperatorRegressionAccess::risk(op,2,r),2,"first mapped risk");
    near(DestroyOperatorRegressionAccess::risk(op,4,r),9,"last mapped risk");
    near(DestroyOperatorRegressionAccess::risk(op,99,r),0,"missing customer");
    Solution s(inst); s.addRoute(r); s.evaluateRoutes(); std::mt19937 rng(1);
    auto removed=op.execute(s,10,rng);
    require(removed.size()==1,"mapping fix preserves single-customer removal policy");
    require(removed[0]==2 || removed[0]==4,"only customers can be removed");
  });
  test("merge accepts disjoint windows feasible in one order",[] {
    auto inst=std::make_shared<Instance>(Nodes{depot(),customer(1,1,10,20,5),customer(2,3,50,60)},100,100,1,1);
    RouteMergingDestroy op(inst);
    require(DestroyOperatorRegressionAccess::compatible(op,route(inst,{1}),route(inst,{2})),"A then B can wait");
    require(DestroyOperatorRegressionAccess::compatible(op,route(inst,{2}),route(inst,{1})),"argument order must not matter");
  });
  test("merge rejects overlap incompatible due to travel time",[] {
    auto inst=std::make_shared<Instance>(Nodes{depot(),customer(1,-4,0,5),customer(2,4,0,5)},100,100,1,1);
    RouteMergingDestroy op(inst);
    require(!DestroyOperatorRegressionAccess::compatible(op,route(inst,{1}),route(inst,{2})),"neither order reaches second window");
  });
  test("merge includes service time and accepts deadline equality",[] {
    auto blocked=std::make_shared<Instance>(Nodes{depot(),customer(1,1,0,5,6),customer(2,1,0,5,6)},100,100,1,1);
    RouteMergingDestroy op(blocked);
    require(!DestroyOperatorRegressionAccess::compatible(op,route(blocked,{1}),route(blocked,{2})),"service makes both orders impossible");
    auto equality=std::make_shared<Instance>(Nodes{depot(),customer(1,1,0,5,5),customer(2,1,0,5,5)},100,100,1,1);
    RouteMergingDestroy eq(equality);
    require(DestroyOperatorRegressionAccess::compatible(eq,route(equality,{1}),route(equality,{2})),"relaxed earliest service can start at deadline");
  });
  std::cout<<"RESULT passed="<<passed<<" failed="<<failed<<'\n';
  return failed?1:0;
}
