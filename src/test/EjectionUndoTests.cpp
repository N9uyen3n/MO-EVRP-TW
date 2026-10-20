#include "ScheduleValidator.h"
#include "alns/LocalSearch.h"
#include "core/Depot.h"
#include <functional>
#include <iostream>
#include <random>

using audit::require;
struct LocalSearchTestAccess {
  static bool eject(LocalSearch& ls,Solution& sol,int depth) { return ls.ejectionChain(sol,depth); }
};
static std::shared_ptr<Instance> fixture() {
  std::vector<std::shared_ptr<Node>> nodes{std::make_shared<Depot>(0,"D",0,0,0,1000)};
  const double demand[]={0,3,5,3,7,2};
  for(int i=1;i<=5;++i) nodes.push_back(std::make_shared<Customer>(i,"C",i%2+1,0,demand[i],0,1000,0));
  return std::make_shared<Instance>(nodes,10,100,1,1);
}
static Route route(const std::shared_ptr<Instance>& inst,std::initializer_list<int> nodes) {
  Route r(0,std::make_shared<Vehicle>(0,inst->getVehicleCapacity(),inst->getVehicleBattery(),1),inst);
  for(int id:nodes) r.addNode(id); r.evaluate(); return r;
}
static void same(const std::vector<Route>& a,const std::vector<Route>& b,const Instance& inst) {
  require(a.size()==b.size(),"route count");
  for(size_t i=0;i<a.size();++i) {
    require(a[i].getNodes()==b[i].getNodes(),"route sequence");
    audit::validateRoute(a[i],inst); audit::validateRoute(b[i],inst);
    const auto& x=a[i].getStates(); const auto& y=b[i].getStates();
    for(size_t p=0;p<x.size();++p) {
      audit::near(x[p].arrivalTime,y[p].arrivalTime,"arrival");
      audit::near(x[p].departureTime,y[p].departureTime,"departure");
      audit::near(x[p].remainingBattery,y[p].remainingBattery,"battery");
      audit::near(x[p].remainingLoad,y[p].remainingLoad,"load");
      audit::near(x[p].chargeAmount,y[p].chargeAmount,"charge");
    }
  }
}
int main() {
  int passed=0,failed=0;
  auto test=[&](const char* name,const std::function<void()>& fn) {
    try { fn(); ++passed; std::cout<<"PASS "<<name<<'\n'; }
    catch(const std::exception& e) { ++failed; std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n'; }
  };
  test("only first modification per route per checkpoint is saved",[] {
    auto inst=fixture(); std::vector<Route> routes{route(inst,{1}),route(inst,{2,3})}; const auto original=routes;
    alns::EjectionUndoStatistics stats; alns::LocalSearchProfiler profiler;
    alns::EjectionUndo undo(routes,true,stats,profiler);
    { alns::EjectionUndo::Checkpoint cp(undo); undo.touch(1); routes[1].removeNode(2);
      undo.touch(1); routes[1].removeNode(1); }
    same(routes,original,*inst); require(stats.savedRoutes==1 && stats.restoredRoutes==1,"one saved route");
  });
  test("committed child is still restored by failed parent",[] {
    auto inst=fixture(); std::vector<Route> routes{route(inst,{1}),route(inst,{2,3})}; const auto original=routes;
    alns::EjectionUndoStatistics stats; alns::LocalSearchProfiler profiler; alns::EjectionUndo undo(routes,true,stats,profiler);
    { alns::EjectionUndo::Checkpoint outer(undo); undo.touch(0); routes[0].clear();
      { alns::EjectionUndo::Checkpoint inner(undo); undo.touch(1); routes[1].removeNode(2); inner.commit(); } }
    same(routes,original,*inst); require(stats.restoredRoutes==2,"nested commit remains undoable");
  });
  test("same route modified at multiple depths restores in reverse order",[] {
    auto inst=fixture(); std::vector<Route> routes{route(inst,{2,3})}; const auto original=routes;
    alns::EjectionUndoStatistics stats; alns::LocalSearchProfiler profiler; alns::EjectionUndo undo(routes,true,stats,profiler);
    { alns::EjectionUndo::Checkpoint outer(undo); undo.touch(0); routes[0].removeNode(2);
      { alns::EjectionUndo::Checkpoint inner(undo); undo.touch(0); routes[0].clear(); }
      require(routes[0].getNodes()==std::vector<int>({0,2,0}),"child rollback restores parent state"); }
    same(routes,original,*inst); require(stats.savedRoutes==2,"separate depth records");
  });
  test("successful parent and child preserve their changes",[] {
    auto inst=fixture(); std::vector<Route> routes{route(inst,{1}),route(inst,{2,3})};
    alns::EjectionUndoStatistics stats; alns::LocalSearchProfiler profiler; alns::EjectionUndo undo(routes,true,stats,profiler);
    { alns::EjectionUndo::Checkpoint outer(undo); undo.touch(0); routes[0].clear();
      { alns::EjectionUndo::Checkpoint inner(undo); undo.touch(1); routes[1].removeNode(2); inner.commit(); }
      outer.commit(); }
    undo.clearHistory();
    require(routes[0].size()==2 && routes[1].getNodes()==std::vector<int>({0,2,0}),"commit keeps edits");
    require(stats.restoredRoutes==0 && stats.commits==2,"no rollback on commit");
  });
  test("exception unwinding restores both checkpoint modes",[] {
    for(bool selective:{false,true}) {
      auto inst=fixture(); std::vector<Route> routes{route(inst,{1}),route(inst,{2,3})}; const auto original=routes;
      alns::EjectionUndoStatistics stats; alns::LocalSearchProfiler profiler; alns::EjectionUndo undo(routes,selective,stats,profiler);
      try { alns::EjectionUndo::Checkpoint outer(undo); undo.touch(0); routes[0].clear();
        alns::EjectionUndo::Checkpoint inner(undo); undo.touch(1); routes[1].clear();
        throw std::runtime_error("rollback probe"); } catch(const std::runtime_error&) {}
      same(routes,original,*inst); require(stats.rollbacks==2,"both scopes rolled back");
    }
  });
  test("dirty route evaluation buffers are restored coherently",[] {
    auto inst=fixture(); std::vector<Route> routes{route(inst,{2,3})}; routes[0].reverseNodes(1,2); const auto original=routes;
    alns::EjectionUndoStatistics stats; alns::LocalSearchProfiler profiler; alns::EjectionUndo undo(routes,true,stats,profiler);
    { alns::EjectionUndo::Checkpoint cp(undo); undo.touch(0); routes[0].clear(); routes[0].evaluate(); }
    same(routes,original,*inst);
  });
  test("actual ejection matches full snapshots and exercises successful recursion",[] {
    auto inst=fixture(); Solution original(inst); original.addRoute(route(inst,{1}));
    original.addRoute(route(inst,{2,3})); original.addRoute(route(inst,{4,5})); original.evaluateRoutes();
    bool exercised=false;
    for(int depth:{0,1,2,3,5}) {
      auto off=original,on=original; LocalSearch a(inst),b(inst);
      a.configureSelectiveEjection(false); b.configureSelectiveEjection(true);
      const bool x=LocalSearchTestAccess::eject(a,off,depth),y=LocalSearchTestAccess::eject(b,on,depth);
      require(x==y,"success decision"); off.evaluateRoutes(); on.evaluateRoutes();
      audit::validateSolution(off,*inst); audit::validateSolution(on,*inst); same(off.getRoutes(),on.getRoutes(),*inst);
      const auto& s=a.getEjectionUndoStatistics(); const auto& t=b.getEjectionUndoStatistics();
      require(s.placeCalls==t.placeCalls && s.branchTrials==t.branchTrials,"search effort unchanged");
      require(t.peakUndoRoutes<=6,"depth bound on undo memory"); exercised|=y && t.branchTrials>0;
    }
    require(exercised,"fixture must reach recursive successful branch");
  });
  test("Euclidean failed and successful chains match across depths",[] {
    std::mt19937 rng(71); std::uint64_t branches=0;
    for(int trial=0;trial<20;++trial) {
      std::vector<std::shared_ptr<Node>> nodes{std::make_shared<Depot>(0,"D",0,0,0,1000)};
      for(int i=1;i<=8;++i) nodes.push_back(std::make_shared<Customer>(i,"C",1+rng()%8,1+rng()%8,1,0,1000,0));
      auto inst=std::make_shared<Instance>(nodes,trial%2?2:3,25,1,1);
      Solution original(inst); for(int i=1;i<=8;i+=2) original.addRoute(route(inst,{i,i+1})); original.evaluateRoutes();
      if(!original.isFeasible()) continue;
      for(int depth:{1,3,5}) {
        auto off=original,on=original; LocalSearch a(inst),b(inst); a.configureSelectiveEjection(false); b.configureSelectiveEjection(true);
        require(LocalSearchTestAccess::eject(a,off,depth)==LocalSearchTestAccess::eject(b,on,depth),"result");
        off.evaluateRoutes(); on.evaluateRoutes(); audit::validateSolution(off,*inst); audit::validateSolution(on,*inst);
        same(off.getRoutes(),on.getRoutes(),*inst);
        require(a.getEjectionUndoStatistics().branchTrials==b.getEjectionUndoStatistics().branchTrials,"same branch trials");
        branches+=b.getEjectionUndoStatistics().branchTrials;
      }
    }
    require(branches>0,"recursive rollback exercised");
  });
  std::cout<<"RESULT passed="<<passed<<" failed="<<failed<<'\n'; return failed?1:0;
}
