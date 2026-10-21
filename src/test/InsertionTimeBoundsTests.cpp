#include "ScheduleValidator.h"
#include "core/Depot.h"
#include <functional>
#include <iostream>
#include <random>

using audit::require;
using audit::near;
using Nodes=std::vector<std::shared_ptr<Node>>;
static Route makeRoute(const std::shared_ptr<Instance>& inst,std::initializer_list<int> ids) {
  Route r(0,std::make_shared<Vehicle>(0,inst->getVehicleCapacity(),inst->getVehicleBattery(),inst->getVehicleEnergyRate()),inst);
  for(int id:ids) r.addNode(id);
  return r;
}
static void compare(const Route& base,int id,size_t pos,const Instance& inst) {
  auto delta=base.checkInsertionCost(id,pos);
  auto full=base;
  full.addNode(id,pos);
  require(delta.isFeasible==full.isFeasible(),"insertion disagrees with canonical full schedule");
  if(!full.isFeasible()) return;
  audit::validateRoute(full,inst);
  near(delta.deltaDistance,full.getTotalDistance()-base.getTotalDistance(),"distance delta");
  near(delta.deltaTime,full.getTotalTime()-base.getTotalTime(),"duration delta");
  near(delta.deltaWaitTime,full.getTotalWaitTime()-base.getTotalWaitTime(),"waiting delta");
  near(delta.deltaChargeAmount,full.getTotalChargeAmount()-base.getTotalChargeAmount(),"charge delta");
  near(delta.deltaEnergyConsumption,full.getTotalEnergyConsumption()-base.getTotalEnergyConsumption(),"energy delta");
}
int main() {
  int passed=0,failed=0;
  const auto test=[&](const char* name,const std::function<void()>& body) {
    try { body(); ++passed; std::cout<<"PASS "<<name<<'\n'; }
    catch(const std::exception& e) { ++failed; std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n'; }
  };
  test("customer deadline constrains start rather than service finish",[] {
    Nodes n{std::make_shared<Depot>(0,"D",0,0,0,100),
      std::make_shared<Customer>(1,"C",1,0,1,0,1,10)};
    auto inst=std::make_shared<Instance>(n,10,100,1,1);
    auto r=makeRoute(inst,{}); compare(r,1,1,*inst);
    require(r.checkInsertionCost(1,1).isFeasible,"service may finish after due date");
  });
  test("prefix and suffix reject actual deadline violations",[] {
    Nodes n{std::make_shared<Depot>(0,"D",0,0,0,100),
      std::make_shared<Customer>(1,"C",1,0,1,0,5,0),
      std::make_shared<Customer>(2,"C",20,0,1,0,10,0),
      std::make_shared<Customer>(3,"C",2,0,1,0,100,10)};
    auto inst=std::make_shared<Instance>(n,10,100,1,1);
    auto r=makeRoute(inst,{1}); Route::resetInsertionTimeBoundsStats();
    compare(r,2,2,*inst); compare(r,3,1,*inst);
    if(Route::insertionTimeBoundsEnabled()) {
      auto stats=Route::getInsertionTimeBoundsStats();
      require(stats.rejections==2 && stats.builds==1,"both rejections reuse one bounds table");
    }
  });
  test("station insertion can repair an infeasible base",[] {
    Nodes n{std::make_shared<Depot>(0,"D",0,0,0,100),
      std::make_shared<Station>(1,"S",5,0,0,100,.5),
      std::make_shared<Customer>(2,"C",9,0,1,0,100,0)};
    auto inst=std::make_shared<Instance>(n,10,12,1,1);
    auto r=makeRoute(inst,{2,1}); require(!r.isFeasible(),"base must lack energy");
    compare(r,1,1,*inst);
    require(r.checkInsertionCost(1,1).isFeasible,"upstream station repairs base");
  });
  test("copied bounds invalidate on add remove reverse clear and assignment",[] {
    Nodes n{std::make_shared<Depot>(0,"D",0,0,0,200),
      std::make_shared<Customer>(1,"C",1,0,1,0,100,2),
      std::make_shared<Customer>(2,"C",2,0,1,0,10,0),
      std::make_shared<Customer>(3,"C",3,0,1,20,100,0),
      std::make_shared<Customer>(4,"C",4,0,1,0,100,5)};
    auto inst=std::make_shared<Instance>(n,10,100,1,1);
    auto r=makeRoute(inst,{1,2,3});
    const auto all=[&](const Route& route) { for(size_t p=1;p<route.size();++p) compare(route,4,p,*inst); };
    all(r); auto copy=r; all(copy);
    copy.removeNode(2); all(copy); all(r);
    copy.addNode(2,1); all(copy); all(r);
    copy.reverseNodes(1,3); all(copy);
    copy.clear(); all(copy);
    copy.addNode(1); all(copy);
    copy=r; all(copy);
    copy=std::move(r); all(copy);
  });
  test("epsilon boundary falls through to the canonical evaluator",[] {
    Nodes n{std::make_shared<Depot>(0,"D",0,0,7,100),
      std::make_shared<Customer>(1,"C",1,0,1,0,8-5e-10,0)};
    auto inst=std::make_shared<Instance>(n,10,100,1,1);
    auto r=makeRoute(inst,{}); compare(r,1,1,*inst);
    require(r.checkInsertionCost(1,1).isFeasible,"canonical epsilon must be retained");
  });
  test("random Euclidean insertions preserve feasibility and every delta",[] {
    std::mt19937 rng(20261021);
    size_t feasible=0,infeasible=0;
    for(int sample=0;sample<400;++sample) {
      const double ready=rng()%10;
      Nodes n{std::make_shared<Depot>(0,"D",0,0,ready,150+rng()%150)};
      for(int id=1;id<=3;++id)
        n.push_back(std::make_shared<Station>(id,"S",rng()%25,rng()%25,rng()%20,80+rng()%160,(rng()%5)*.5));
      for(int id=4;id<=11;++id) {
        const double e=rng()%90;
        n.push_back(std::make_shared<Customer>(id,"C",rng()%25,rng()%25,1+rng()%4,e,e+5+rng()%140,rng()%15));
      }
      auto inst=std::make_shared<Instance>(n,15,30+rng()%100,.5+(rng()%3)*.5,.5+(rng()%4)*.5);
      auto r=makeRoute(inst,{});
      for(int id:{1,4,5,2,6,7,3}) if(rng()%4) r.addNode(id);
      for(int id:{1,2,3,8,9,10,11}) for(size_t pos=1;pos<r.size();++pos) {
        compare(r,id,pos,*inst);
        if(r.checkInsertionCost(id,pos).isFeasible) ++feasible; else ++infeasible;
      }
    }
    require(feasible>50 && infeasible>1000,"exercise both accepted and rejected schedules");
    std::cout<<"DIFFERENTIAL feasible="<<feasible<<" infeasible="<<infeasible<<'\n';
  });
  std::cout<<"RESULT passed="<<passed<<" failed="<<failed<<'\n';
  return failed?1:0;
}
