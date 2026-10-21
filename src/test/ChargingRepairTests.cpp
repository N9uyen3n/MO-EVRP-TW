#include "ScheduleValidator.h"
#include "alns/ChargingRepair.h"
#include "alns/LocalSearch.h"
#include "core/Depot.h"
#include <functional>
#include <iostream>
#include <limits>

using audit::require;
using audit::near;
using Nodes=std::vector<std::shared_ptr<Node>>;

struct LocalSearchTestAccess {
  static bool relocate(LocalSearch& search,Solution& sol,int source,int pos,int target,int insertion) {
    return search.tryRepairRelocate(sol,source,pos,target,insertion);
  }
  static bool searchRelocate(LocalSearch& search,Solution& sol) {
    search.updateSearchContext(sol);
    return search.searchRelocate(sol,LocalSearchWeights{},search.searchContext_);
  }
  static bool interTwoOpt(LocalSearch& search,Solution& sol) {
    search.updateSearchContext(sol);
    return search.searchInterTwoOpt(sol,LocalSearchWeights{},search.searchContext_);
  }
};

static std::shared_ptr<Depot> depot(double ready=0) {
  return std::make_shared<Depot>(0,"D",0,0,ready,1000);
}
static std::shared_ptr<Customer> customer(int id,double x,double y=0,double due=1000,double demand=1) {
  return std::make_shared<Customer>(id,"C",x,y,demand,0,due,0);
}
static std::shared_ptr<Station> station(int id,double x,double y=0) {
  return std::make_shared<Station>(id,"S",x,y,0,1000,1);
}
static Route route(const std::shared_ptr<Instance>& inst,const std::vector<int>& ids) {
  Route r(0,std::make_shared<Vehicle>(0,inst->getVehicleCapacity(),inst->getVehicleBattery(),inst->getVehicleEnergyRate()),inst);
  for(int id:ids) r.addNode(id);
  r.evaluate(); return r;
}
static alns::ChargingRepairBudget budget(int visits=6) {
  alns::ChargingRepairBudget result;
  result.maxEvaluations=1000; result.maxStations=visits;
  result.stationCandidates=100; result.beamWidth=64;
  result.maxDuration=std::chrono::milliseconds(1000);
  return result;
}
static std::shared_ptr<Instance> threeStations() {
  // With Q=10, serving x=14 then x=13 requires at least three station visits.
  // The stations at x=6 and x=12 allow that without changing customer order.
  return std::make_shared<Instance>(Nodes{depot(7),customer(1,14),customer(2,13),
      station(3,6),station(4,12)},100,10,1,2);
}
static std::shared_ptr<Instance> relocationInstance() {
  return std::make_shared<Instance>(Nodes{depot(),customer(1,11),station(2,10),customer(3,5)},100,20,1,1);
}
static Solution relocationSolution(const std::shared_ptr<Instance>& inst) {
  Solution sol(inst); sol.addRoute(route(inst,{2,1})); sol.addRoute(route(inst,{3}));
  sol.evaluateRoutes(); audit::validateSolution(sol,*inst); return sol;
}

int main() {
  int passed=0,failed=0;
  auto test=[&](const std::string& name,const std::function<void()>& body) {
    try { body(); ++passed; std::cout<<"PASS "<<name<<'\n'; }
    catch(const std::exception& error) { ++failed; std::cout<<"FAIL "<<name<<": "<<error.what()<<'\n'; }
  };
  test("three mandatory charging visits on a symmetric Euclidean route",[] {
    auto inst=threeStations(); auto original=route(inst,{1,2});
    const auto nodes=original.getNodes();
    auto result=alns::repairChargingForCustomerSequence(original,inst,{1,2},budget(3));
    require(result.status==alns::ChargingRepairStatus::SUCCESS && result.route.has_value(),"repair failed");
    require(std::count_if(result.route->getNodes().begin(),result.route->getNodes().end(),
        [&](int id){return inst->getNodeType(id)==NodeType::STATION;})==3,"expected three charging visits");
    require(result.route->getCustomers()==std::vector<int>({1,2}),"customer order changed");
    audit::validateRoute(*result.route,*inst);
    near(result.route->getTotalDistance(),28,"repaired distance");
    near(result.route->getTotalTime(),32,"travel time must respect velocity");
    near(result.route->getStates().front().departureTime,7,"depot ready time");
    require(original.getNodes()==nodes,"repair modified input");
  });
  test("station visit cap and evaluation cap are enforced transactionally",[] {
    auto inst=threeStations(); auto original=route(inst,{1,2}); auto nodes=original.getNodes();
    auto capped=alns::repairChargingForCustomerSequence(original,inst,{1,2},budget(2));
    require(!capped.route && capped.status==alns::ChargingRepairStatus::NO_CANDIDATE,"two stations cannot suffice");
    auto limited=budget(3); limited.maxEvaluations=1;
    auto result=alns::repairChargingForCustomerSequence(original,inst,{1,2},limited);
    require(!result.route && result.status==alns::ChargingRepairStatus::BUDGET_EXHAUSTED,"evaluation budget status");
    require(result.evaluations==1,"evaluation cap exceeded");
    require(original.getNodes()==nodes,"failed repair changed source");
  });
  test("zero evaluation and expired deadline do not evaluate candidates",[] {
    auto inst=threeStations(); auto original=route(inst,{1,2});
    for(int kind=0;kind<3;++kind) {
      auto b=budget();
      if(kind==0) b.maxEvaluations=0;
      if(kind==1) b.deadline=std::chrono::steady_clock::now()-std::chrono::milliseconds(1);
      if(kind==2) b.maxDuration=std::chrono::milliseconds(0);
      auto r=alns::repairChargingForCustomerSequence(original,inst,{1,2},b);
      require(!r.route && r.budgetExhausted && r.evaluations==0,"deadline/evaluation budget ignored");
    }
  });
  test("empty and new customer sequences without charging",[] {
    auto inst=std::make_shared<Instance>(Nodes{depot(),customer(1,2),customer(2,3)},100,100,1,1);
    auto original=route(inst,{1}); auto b=budget(0); b.stationCandidates=0;
    for(const auto& order:std::vector<std::vector<int>>{{},{1},{2,1}}) {
      auto r=alns::repairChargingForCustomerSequence(original,inst,order,b);
      require(r.route.has_value(),"direct route rejected");
      require(r.route->getCustomers()==order,"empty/new customer order changed");
      audit::validateRoute(*r.route,*inst);
    }
    require(original.getCustomers()==std::vector<int>({1}),"input customer set changed");
  });
  test("invalid customer IDs duplicates and invalid budgets are rejected",[] {
    auto inst=threeStations(); auto original=route(inst,{1,2});
    for(const auto& order:std::vector<std::vector<int>>{{1,1},{-1},{6},{3},{0}}) {
      auto r=alns::repairChargingForCustomerSequence(original,inst,order,budget());
      require(!r.route && r.status==alns::ChargingRepairStatus::INVALID_INPUT,"invalid customer input accepted");
    }
    auto b=budget(); b.maxStations=-1;
    require(alns::repairChargingForCustomerSequence(original,inst,{1},b).status==alns::ChargingRepairStatus::INVALID_INPUT,"invalid budget accepted");
    b=budget(); b.maxEvaluations=-1;
    require(alns::repairChargingForCustomerSequence(original,inst,{1},b).status==alns::ChargingRepairStatus::INVALID_INPUT,"negative evaluation budget accepted");
    require(alns::repairChargingForCustomerSequence(original,nullptr,{1},budget()).status==alns::ChargingRepairStatus::INVALID_INPUT,"missing instance accepted");
  });
  test("delivery capacity and impossible time window fail",[] {
    auto inst=std::make_shared<Instance>(Nodes{depot(),customer(1,5,0,1,6),customer(2,2,0,1000,6)},10,100,1,1);
    auto original=route(inst,{});
    require(!alns::repairChargingForCustomerSequence(original,inst,{1,2},budget()).route,"global delivery capacity ignored");
    require(!alns::repairChargingForCustomerSequence(original,inst,{1},budget(0)).route,"time window ignored");
  });
  test("nonconsecutive repeated station visits are supported",[] {
    auto inst=std::make_shared<Instance>(Nodes{depot(),customer(1,9),station(2,5)},100,10,1,1);
    auto r=alns::repairChargingForCustomerSequence(route(inst,{1}),inst,{1},budget(2));
    require(r.route && r.route->getNodes()==std::vector<int>({0,2,1,2,0}),"repeated station repair missing");
    audit::validateRoute(*r.route,*inst);
  });
  test("small charging configuration agrees with exhaustive enumeration",[] {
    auto inst=std::make_shared<Instance>(Nodes{depot(),customer(1,9),station(2,5),station(3,7)},100,10,1,1);
    double optimum=std::numeric_limits<double>::infinity();
    std::vector<int> body;
    std::function<void(bool,int)> enumerate=[&](bool served,int visits) {
      if(served) {
        auto trial=route(inst,body);
        if(trial.isFeasible()) { audit::validateRoute(trial,*inst); optimum=std::min(optimum,trial.getTotalDistance()); }
      } else { body.push_back(1); enumerate(true,visits); body.pop_back(); }
      if(visits<2) for(int s:{2,3}) {
        if(!body.empty() && body.back()==s) continue;
        body.push_back(s); enumerate(served,visits+1); body.pop_back();
      }
    };
    enumerate(false,0);
    auto result=alns::repairChargingForCustomerSequence(route(inst,{1}),inst,{1},budget(2));
    require(result.route.has_value(),"beam search missed exhaustive feasible fixture");
    near(result.route->getTotalDistance(),optimum,"distance vs exhaustive fixture");
    near(optimum,18,"fixture optimum"); audit::validateRoute(*result.route,*inst);
  });
  test("station candidates use symmetric Euclidean detour",[] {
    auto inst=std::make_shared<Instance>(Nodes{depot(),customer(1,10),station(2,5),station(3,5,3)},100,100,1,1);
    require(inst->getCandidateStations(0,1,1)==std::vector<int>({2}),"forward station ranking");
    require(inst->getCandidateStations(1,0,1)==std::vector<int>({2}),"symmetric station ranking");
    require(inst->getCandidateStations(0,1,2)==std::vector<int>({2,3}),"cached detour ranking");
    require(inst->getCandidateStations(0,1,0).empty(),"zero candidates");
  });
  test("narrow station list widens within the shared budget",[] {
    auto inst=std::make_shared<Instance>(Nodes{depot(),customer(1,14),station(2,12),station(3,6)},100,10,1,1);
    // Both stations have equal detour. The first one by ID cannot be reached
    // from the depot. Widening finds x=6 and allows three charging visits.
    auto b=budget(3); b.stationCandidates=1;
    auto r=alns::repairChargingForCustomerSequence(route(inst,{1}),inst,{1},b);
    require(r.route.has_value(),"station list was not widened");
    require(r.evaluations<=b.maxEvaluations,"widening exceeded total budget");
    audit::validateRoute(*r.route,*inst);
    near(r.route->getTotalDistance(),28,"widened Euclidean route");
  });
  test("relocate repair is disabled by default and saves an improving move",[] {
    auto inst=relocationInstance(); auto sol=relocationSolution(inst); const auto hash=sol.getHash();
    LocalSearch search(inst);
    require(!LocalSearchTestAccess::relocate(search,sol,0,2,1,2),"repair should be disabled");
    require(sol.getHash()==hash,"disabled helper modified solution");
    search.configureChargingRepair(true,budget());
    require(LocalSearchTestAccess::relocate(search,sol,0,2,1,2),"station repair did not rescue relocation");
    sol.evaluateRoutes(); audit::validateSolution(sol,*inst);
    near(sol.getTotalDistance(),22,"relocated distance"); near(sol.getTotalVehicles(),1,"reduced fleet");
    require(search.getChargingRepairStatistics().relocateAccepted==1,"accepted relocate counter");
    require(search.getChargingRepairStatistics().vehiclesReduced==1,"fleet reduction counter");
  });
  test("actual relocate and inter-two-opt loops reach charging repair",[] {
    auto inst=relocationInstance();
    for(bool twoOpt:{false,true}) {
      auto sol=relocationSolution(inst); LocalSearch search(inst); search.configureChargingRepair(true,budget());
      search.configureOrOptCache(alns::OrOptCacheMode::Observe);
      bool moved=twoOpt?LocalSearchTestAccess::interTwoOpt(search,sol):LocalSearchTestAccess::searchRelocate(search,sol);
      require(moved,"operator filters blocked station repair");
      sol.evaluateRoutes(); audit::validateSolution(sol,*inst);
      near(sol.getTotalDistance(),22,"operator repaired cost");
      require(twoOpt?search.getChargingRepairStatistics().interTwoOptAccepted==1:
                     search.getChargingRepairStatistics().relocateAccepted==1,"operator acceptance statistics");
      require(search.getOrOptCacheStatistics().generationChanges>=2,"repair commit must advance both route generations");
    }
  });
  test("real station detour cancels provisional improvement without commit",[] {
    auto inst=std::make_shared<Instance>(Nodes{depot(),customer(1,6),station(2,6,6),customer(3,0,6)},100,12,1,1);
    Solution sol(inst); sol.addRoute(route(inst,{1})); sol.addRoute(route(inst,{3})); sol.evaluateRoutes();
    const auto hash=sol.getHash(); LocalSearch search(inst); search.configureChargingRepair(true,budget());
    require(!LocalSearchTestAccess::relocate(search,sol,0,1,1,2),"equal repaired cost was committed");
    require(sol.getHash()==hash,"rejected move changed solution");
    require(search.getChargingRepairStatistics().feasible>0,"fixture did not exercise feasible repair");
    sol.evaluateRoutes(); audit::validateSolution(sol,*inst); near(sol.getTotalDistance(),24,"rollback distance");
  });
  test("unrepairable target and expired LS deadline preserve solution",[] {
    auto inst=std::make_shared<Instance>(Nodes{depot(),customer(1,6),customer(2,0,6)},100,12,1,1);
    Solution sol(inst); sol.addRoute(route(inst,{1})); sol.addRoute(route(inst,{2})); sol.evaluateRoutes();
    const auto hash=sol.getHash(); LocalSearch search(inst); search.configureChargingRepair(true,budget());
    require(!LocalSearchTestAccess::relocate(search,sol,0,1,1,2),"unrepairable target accepted");
    require(search.getChargingRepairStatistics().calls>0,"fixture did not attempt charging repair");
    require(sol.getHash()==hash,"failed two-route repair changed solution");
    search.setChargingRepairDeadline(std::chrono::steady_clock::now()-std::chrono::milliseconds(1));
    const auto calls=search.getChargingRepairStatistics().calls;
    require(!LocalSearchTestAccess::relocate(search,sol,0,1,1,2),"deadline ignored");
    require(search.getChargingRepairStatistics().calls==calls,"repair called after global deadline");
    audit::validateSolution(sol,*inst);
  });
  std::cout<<"RESULT passed="<<passed<<" failed="<<failed<<'\n';
  return failed?1:0;
}
