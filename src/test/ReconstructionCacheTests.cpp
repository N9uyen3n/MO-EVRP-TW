#include "ScheduleValidator.h"
#include "alns/LocalSearch.h"
#include "core/Depot.h"
#include <functional>
#include <iostream>

using alns::ReconstructionCache;
using alns::ReconstructionCacheMode;
using audit::require;

struct LocalSearchTestAccess {
  static void session(LocalSearch& search, const std::function<void()>& fn) {
    ReconstructionCache::Session session(search.reconstructionCache_); fn();
  }
};
static std::shared_ptr<Instance> fixture(bool station=false) {
  std::vector<std::shared_ptr<Node>> nodes{std::make_shared<Depot>(0,"D",0,0,0,1000),
    std::make_shared<Customer>(1,"C1",2,0,1,0,1000,0),
    std::make_shared<Customer>(2,"C2",20,0,1,0,1000,0),
    std::make_shared<Customer>(3,"C3",3,0,1,0,1000,0)};
  if(station) nodes.push_back(std::make_shared<Station>(4,"S",10,0,0,1000,1));
  return std::make_shared<Instance>(nodes,100,station?20:10,1,1);
}
static Route route(const std::shared_ptr<Instance>& inst, int customer=1, double battery=-1) {
  Route result(0,std::make_shared<Vehicle>(0,100,battery<0?inst->getVehicleBattery():battery,1),inst);
  result.addNode(customer); result.evaluate(); return result;
}
int main() {
  int passed=0,failed=0;
  auto test=[&](const char* name,const std::function<void()>& fn) {
    try { fn(); ++passed; std::cout<<"PASS "<<name<<'\n'; }
    catch(const std::exception& e) { ++failed; std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n'; }
  };
  test("failure hit preserves input and skips only a repeated computation",[] {
    auto inst=fixture(); auto r=route(inst); const auto original=r.getNodes();
    LocalSearch ls(inst); ls.configureReconstructionCache(ReconstructionCacheMode::Enabled);
    LocalSearchTestAccess::session(ls,[&] {
      require(!ls.reconstructRouteWithTwoStations(r,{2},2),"expected failure");
      require(!ls.reconstructRouteWithTwoStations(r,{2},2),"expected repeated failure");
    });
    const auto& s=ls.getReconstructionCacheStatistics();
    require(s.requests==2 && s.computations==1 && s.hits==1,"one actual computation");
    require(r.getNodes()==original && r.isFeasible(),"failed reconstruction changed input");
  });
  test("observe records repetitions without skipping",[] {
    auto inst=fixture(); auto r=route(inst); LocalSearch ls(inst);
    ls.configureReconstructionCache(ReconstructionCacheMode::Observe);
    LocalSearchTestAccess::session(ls,[&] { for(int i=0;i<2;++i)
      require(!ls.reconstructRouteWithTwoStations(r,{2},2),"expected failure"); });
    const auto& s=ls.getReconstructionCacheStatistics();
    require(s.computations==2 && s.repeatedFailures==1 && s.hits==0,"observe does all work");
  });
  test("cache expires at entry point boundary",[] {
    auto inst=fixture(); auto r=route(inst); LocalSearch ls(inst);
    ls.configureReconstructionCache(ReconstructionCacheMode::Enabled);
    for(int i=0;i<2;++i) LocalSearchTestAccess::session(ls,[&] {
      require(!ls.reconstructRouteWithTwoStations(r,{2},2),"expected failure"); });
    require(ls.getReconstructionCacheStatistics().computations==2,"fresh session must compute");
    require(ls.getReconstructionCacheStatistics().hits==0,"no cross-solution cache hit");
    require(!ls.reconstructRouteWithTwoStations(r,{2},2),"public helper outside session still works");
    require(ls.getReconstructionCacheStatistics().requests==2,"outside session uses original path");
  });
  test("duplicate route IDs cannot alias different sequences",[] {
    auto inst=fixture(); auto a=route(inst,1),b=route(inst,3); LocalSearch ls(inst);
    ls.configureReconstructionCache(ReconstructionCacheMode::Enabled);
    LocalSearchTestAccess::session(ls,[&] {
      require(!ls.reconstructRouteWithTwoStations(a,{2},2),"first failure");
      require(!ls.reconstructRouteWithTwoStations(b,{2},2),"second failure");
    });
    require(ls.getReconstructionCacheStatistics().computations==2 &&
            ls.getReconstructionCacheStatistics().hits==0,"sequence is part of key");
  });
  test("incoming order and station bound are distinct keys",[] {
    auto inst=fixture(); auto r=route(inst); LocalSearch ls(inst);
    ls.configureReconstructionCache(ReconstructionCacheMode::Enabled);
    LocalSearchTestAccess::session(ls,[&] {
      for(const auto& incoming:std::vector<std::vector<int>>{{2,3},{3,2}})
        for(int bound:{0,2}) require(!ls.reconstructRouteWithTwoStations(r,incoming,bound),"failure");
    });
    require(ls.getReconstructionCacheStatistics().computations==4 &&
            ls.getReconstructionCacheStatistics().hits==0,"ordered input and limit included");
  });
  test("larger station bound can succeed after cached failure",[] {
    auto inst=fixture(true); auto r=route(inst); LocalSearch ls(inst);
    ls.configureReconstructionCache(ReconstructionCacheMode::Enabled);
    LocalSearchTestAccess::session(ls,[&] {
      require(!ls.reconstructRouteWithTwoStations(r,{2},0),"no station cannot reach customer");
      require(ls.reconstructRouteWithTwoStations(r,{2},2),"two station visits must remain searchable");
    });
    require(r.isFeasible() && ls.getReconstructionCacheStatistics().hits==0,"bound invalidation");
  });
  test("vehicle capacity battery and energy rate cannot reuse failure",[] {
    auto inst=fixture(); auto low=route(inst),high=route(inst,1,100); LocalSearch ls(inst);
    Route small(0,std::make_shared<Vehicle>(0,1,100,1),inst); small.addNode(1);
    Route efficient(0,std::make_shared<Vehicle>(0,100,10,0.1),inst); efficient.addNode(1);
    ls.configureReconstructionCache(ReconstructionCacheMode::Enabled);
    LocalSearchTestAccess::session(ls,[&] {
      require(!ls.reconstructRouteWithTwoStations(low,{2},2),"small battery fails");
      require(!ls.reconstructRouteWithTwoStations(small,{2},2),"small capacity fails");
      require(ls.reconstructRouteWithTwoStations(high,{2},2),"large battery succeeds");
      require(ls.reconstructRouteWithTwoStations(efficient,{2},2),"lower consumption succeeds");
    });
    require(high.isFeasible() && ls.getReconstructionCacheStatistics().hits==0,"vehicle included");
  });
  test("successes are recomputed and cache capacity is bounded",[] {
    auto inst=fixture(); auto r=route(inst); LocalSearch ls(inst);
    ls.configureReconstructionCache(ReconstructionCacheMode::Enabled,1);
    LocalSearchTestAccess::session(ls,[&] {
      for(int bound:{0,1,0}) require(!ls.reconstructRouteWithTwoStations(r,{2},bound),"failure");
      for(int i=0;i<2;++i) { auto candidate=r;
        require(ls.reconstructRouteWithTwoStations(candidate,{3},0),"near customer fits"); }
    });
    const auto& s=ls.getReconstructionCacheStatistics();
    require(s.peakEntries==1 && s.capacityClears==2,"bounded eviction");
    require(s.computations==5 && s.successes==2 && s.hits==0,"no success memoization");
  });
  test("disabled zero-capacity and oversized requests keep original behavior",[] {
    auto inst=fixture();
    for(auto mode:{ReconstructionCacheMode::Off,ReconstructionCacheMode::Enabled}) {
      LocalSearch ls(inst); ls.configureReconstructionCache(mode,0); auto r=route(inst);
      LocalSearchTestAccess::session(ls,[&] { for(int i=0;i<2;++i)
        require(!ls.reconstructRouteWithTwoStations(r,{2},2),"original failure"); });
      require(ls.getReconstructionCacheStatistics().hits==0,"disabled");
    }
    LocalSearch ls(inst); ls.configureReconstructionCache(ReconstructionCacheMode::Enabled); auto r=route(inst);
    LocalSearchTestAccess::session(ls,[&] {
      require(!ls.reconstructRouteWithTwoStations(r,std::vector<int>(513,2),2),"capacity failure"); });
    require(ls.getReconstructionCacheStatistics().oversized==1 &&
            ls.getReconstructionCacheStatistics().peakEntries==0,"large key bypass");
  });
  test("cache on and off preserve full local search and coverage",[] {
    auto inst=fixture(true);
    Solution off(inst); off.addRoute(route(inst,1)); off.addRoute(route(inst,3));
    // Customer 2 is initially served with two station visits.
    Route far(0,std::make_shared<Vehicle>(0,100,20,1),inst);
    for(int id:{4,2,4}) far.addNode(id);
    off.addRoute(far); off.evaluateRoutes(); auto on=off;
    LocalSearch a(inst),b(inst); b.configureReconstructionCache(ReconstructionCacheMode::Enabled);
    a.run(off); b.run(on);
    // Match ALNSSolver's post-LS aggregation before reading solution metrics.
    off.evaluateRoutes(); on.evaluateRoutes();
    audit::validateSolution(off,*inst); audit::validateSolution(on,*inst);
    require(off.getHash()==on.getHash(),"cache changed moves");
    audit::near(off.getTotalDistance(),on.getTotalDistance(),"distance");
    b.resetReconstructionCache();
    require(b.getReconstructionCacheStatistics().requests==0,"statistics reset");
  });
  std::cout<<"RESULT passed="<<passed<<" failed="<<failed<<'\n';
  return failed?1:0;
}
