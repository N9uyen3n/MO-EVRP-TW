#include "ScheduleValidator.h"
#include "alns/ALNSSolver.h"
#include "core/Customer.h"
#include "core/Depot.h"
#include <functional>
#include <iostream>
#include <random>

struct LocalSearchTestAccess {
  static MoveEvaluation delta(LocalSearch& search,const Solution& solution,const MoveDescriptor& move) {
    return search.evaluateRelocateDelta(solution,move);
  }
  static MoveEvaluation evaluate(LocalSearch& search,const Solution& solution,MoveDescriptor move) {
    search.evaluateMove(solution,move,LocalSearchWeights{}); return move.eval;
  }
  static bool eject(LocalSearch& search,Solution& solution,int depth) { return search.ejectionChain(solution,depth); }
};
static std::shared_ptr<Instance> instance(int customers,double capacity=100,double battery=1000) {
  std::vector<std::shared_ptr<Node>> nodes{std::make_shared<Depot>(0,"D",0,0,0,1000)};
  for(int i=1;i<=customers;++i) nodes.push_back(std::make_shared<Customer>(i,"C",i,0,1,0,1000,0));
  return std::make_shared<Instance>(nodes,capacity,battery,1,1);
}
static Route makeRoute(const std::shared_ptr<Instance>& inst,std::initializer_list<int> ids) {
  Route r(0,std::make_shared<Vehicle>(0,inst->getVehicleCapacity(),inst->getVehicleBattery(),1),inst);
  for(int id:ids) r.addNode(id);
  r.evaluate(); return r;
}
int main() {
  int passed=0,failed=0;
  auto test=[&](const std::string& name,const std::function<void()>& run) {
    try { run(); ++passed; std::cout<<"PASS "<<name<<'\n'; }
    catch(const std::exception& e) { ++failed; std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n'; }
  };
  test("intra-relocate delta agrees in both directions at full capacity",[] {
    auto inst=instance(4,4); const int n=5;
    std::vector<double> d(n*n),t(n*n);
    for(int i=0;i<n;++i) for(int j=0;j<n;++j) if(i!=j) d[i*n+j]=t[i*n+j]=1+(i*17+j*11)%13;
    inst->setTravelMatrices(d,t);
    Solution sol(inst); sol.addRoute(makeRoute(inst,{1,2,3,4})); sol.evaluateRoutes();
    LocalSearch search(inst);
    for(int from=1;from<=4;++from) for(int to=1;to<=5;++to) {
      if(to==from || to==from+1) continue;
      MoveDescriptor move; move.type=MoveType::INTRA_RELOCATE;
      move.routeIdx1=move.routeIdx2=0; move.nodeIdx1=from; move.nodeIdx2=to;
      auto estimate=LocalSearchTestAccess::delta(search,sol,move);
      audit::require(estimate.isFeasible,"full-capacity relocation rejected as extra load");
      Route changed=sol.getRoutes()[0]; int customer=changed.getNodeAt(from);
      changed.removeNode(from); changed.addNode(customer,to-(from<to));
      audit::near(estimate.distanceDelta,changed.getTotalDistance()-sol.getTotalDistance(),"relocate delta");
    }
  });
  test("directed two-opt accounts for reversed internal arcs",[] {
    auto inst=instance(4); const int n=5; std::vector<double> d(n*n,3),t(n*n,1);
    for(int i=0;i<n;++i) d[i*n+i]=t[i*n+i]=0;
    d[0*n+1]=d[3*n+4]=d[4*n+0]=1;
    d[1*n+2]=d[2*n+3]=20; d[3*n+2]=d[2*n+1]=1;
    inst->setTravelMatrices(d,t);
    Solution sol(inst); sol.addRoute(makeRoute(inst,{1,2,3,4})); sol.evaluateRoutes();
    LocalSearch search(inst); MoveDescriptor move;
    move.type=MoveType::INTRA_TWO_OPT; move.routeIdx1=move.routeIdx2=0; move.nodeIdx1=1; move.nodeIdx2=3;
    const auto evaluation=LocalSearchTestAccess::evaluate(search,sol,move);
    audit::require(evaluation.isFeasible,"boundary-only filter missed directed improvement");
    audit::near(evaluation.distanceDelta,-34,"directed reversal delta");
  });
  test("inter-relocate validates source after removal",[] {
    auto inst=instance(3,100,10); const int n=4; std::vector<double> d(n*n,1),t(n*n,1);
    for(int i=0;i<n;++i) d[i*n+i]=t[i*n+i]=0;
    d[0*n+2]=20; inst->setTravelMatrices(d,t);
    Solution sol(inst); sol.addRoute(makeRoute(inst,{1,2})); sol.addRoute(makeRoute(inst,{3})); sol.evaluateRoutes();
    audit::validateSolution(sol,*inst);
    LocalSearch search(inst); MoveDescriptor move;
    move.type=MoveType::INTER_RELOCATE; move.routeIdx1=0; move.routeIdx2=1; move.nodeIdx1=1; move.nodeIdx2=1;
    audit::require(!LocalSearchTestAccess::evaluate(search,sol,move).isFeasible,"source bypass exceeds battery");
  });
  test("local search probability has no 22-25 percent gap",[] {
    alns::ALNSConfig config; config.localSearchIntensity=25; config.maxIterations=25000;
    for(int i:{0,2000,2001,5499,5500,6000,6249,6250,17499,17500,24999})
      audit::require(config.localSearchProbability(i,8)>0,"unexpected zero intensity");
    config.localSearchIntensity=100;
    audit::require(config.localSearchProbability(0,8)==100,"probability must be capped");
  });
  test("failed ejection preserves all routes and customers",[] {
    auto inst=instance(6,2); Solution original(inst);
    original.addRoute(makeRoute(inst,{1,2})); original.addRoute(makeRoute(inst,{3,4})); original.addRoute(makeRoute(inst,{5,6}));
    original.evaluateRoutes(); LocalSearch search(inst);
    for(int depth:{0,1,2,3,5}) {
      auto sol=original;
      audit::require(!LocalSearchTestAccess::eject(search,sol,depth),"impossible fleet capacity");
      audit::validateSolution(sol,*inst);
      audit::near(sol.getTotalDistance(),original.getTotalDistance(),"failed chain rollback");
    }
  });
  test("local search output is independently feasible",[] {
    auto inst=instance(6,3); Solution sol(inst);
    sol.addRoute(makeRoute(inst,{1,5})); sol.addRoute(makeRoute(inst,{2,6})); sol.addRoute(makeRoute(inst,{3,4}));
    sol.evaluateRoutes(); LocalSearch search(inst); search.run(sol); sol.evaluateRoutes();
    audit::validateSolution(sol,*inst);
  });
  test("ejection rollback and coverage on directed instances",[] {
    std::mt19937 rng(42);
    for(int iteration=0;iteration<20;++iteration) {
      auto inst=instance(8,3,20); const int n=9;
      std::vector<double> d(n*n),t(n*n);
      for(int i=0;i<n;++i) for(int j=0;j<n;++j) if(i!=j) {
        d[i*n+j]=1+rng()%5; t[i*n+j]=1+rng()%7;
      }
      inst->setTravelMatrices(d,t);
      Solution original(inst);
      original.addRoute(makeRoute(inst,{1,2})); original.addRoute(makeRoute(inst,{3,4}));
      original.addRoute(makeRoute(inst,{5,6})); original.addRoute(makeRoute(inst,{7,8})); original.evaluateRoutes();
      for(int depth:{0,1,3,5}) {
        auto sol=original; LocalSearch search(inst);
        const bool changed=LocalSearchTestAccess::eject(search,sol,depth);
        sol.evaluateRoutes(); audit::validateSolution(sol,*inst);
        if(!changed) audit::require(sol.getHash()==original.getHash(),"rollback changes node sequence");
        else audit::require(sol.getNumRoutes()<original.getNumRoutes(),"successful ejection must remove route");
      }
    }
  });
  test("solver reports actual iterations and stop reason",[] {
    auto inst=instance(3); alns::ALNSConfig config;
    config.maxIterations=3; config.randomSeed=1; config.useLocalSearch=false;
    alns::ALNSSolver solver(inst,config,"","test");
    const auto front=solver.solve();
    audit::require(!front.empty(),"solver front");
    const auto& stats=solver.getRunStatistics();
    audit::require(stats.completedIterations==3,"actual iteration count");
    audit::require(stats.stopReason=="iteration_limit","stop reason");
    audit::require(stats.totalMs>=stats.searchMs,"total includes initialization");
    for(const auto& sol:front) audit::validateSolution(sol,*inst);
  });
  std::cout<<"RESULT passed="<<passed<<" failed="<<failed<<'\n';
  return failed?1:0;
}
