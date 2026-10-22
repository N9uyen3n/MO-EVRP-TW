#include "ScheduleValidator.h"
#include "alns/ParetoArchive.h"
#include "alns/SearchObjectives.h"
#include "alns/ScatterSearch.h"
#include "alns/operators/repair/AdaptiveInsertion.h"
#include "core/Customer.h"
#include "core/Depot.h"
#include <functional>
#include <iostream>

// Objective-only fixtures isolate search policy; real schedule replay is tested separately.
struct SearchObjectivesTestAccess {
  static Solution point(int vehicles, double distance, double gini, double time) {
    const auto instance=std::make_shared<Instance>(
      std::vector<std::shared_ptr<Node>>{
        std::make_shared<Depot>(0,"D",0,0,0,1000),
        std::make_shared<Customer>(1,"C",0,0,1,0,1000,time)},100,1000,1,1);
    Solution s(instance);
    // getMaxTime() reads route schedules, whereas full dominance uses its cache.
    Route r(0,std::make_shared<Vehicle>(0,100,1000,1),instance);
    r.addNode(1,1); r.evaluate(); s.addRoute(r);
    s.totalVehicles=vehicles; s.totalDistance=distance;
    s.workloadGini=gini; s.maxTime=time;
    return s;
  }
};
namespace alns {
struct SearchPolicyTestAccess {
  static bool workload(const AdaptiveInsertion& op, double draw) {
    return op.selectMode(draw)==AdaptiveInsertion::Mode::WORKLOAD_FOCUSED;
  }
  static auto crowding(const std::vector<Solution>& front) {
    return ScatterSearch::computeCrowdingDistance(front);
  }
};
}
int main() {
  namespace policy = alns::search;
  int passed=0, failed=0;
  auto test=[&](const char* name, const std::function<void()>& body) {
    try { body(); ++passed; std::cout<<"PASS "<<name<<'\n'; }
    catch(const std::exception& e) { ++failed; std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n'; }
  };
  const auto point=SearchObjectivesTestAccess::point;
  test("fleet priority and distance/time trade-offs",[&] {
    auto a=point(2,200,.8,200), b=point(3,100,0,100);
    audit::require(policy::dominates(a,b) && !policy::dominates(b,a),"fleet priority");
    audit::require(!policy::dominates(point(2,100,.2,200),point(2,200,.2,100)),"D/T trade-off retained");
  });
  test("Gini cannot veto or create NoEQ dominance",[&] {
    auto a=point(2,100,.9,100), b=point(2,110,.1,110);
    audit::require(policy::dominates(a,b)==!policy::equityEnabled,"Gini veto policy");
    auto c=point(2,100,.1,100);
    audit::require(policy::dominates(c,a)==policy::equityEnabled,"Gini-only improvement policy");
    audit::require(!a.dominates(b) && c.dominates(a),"reported four-objective dominance retained");
  });
  test("dominance tolerances inherited",[&] {
    auto a=point(2,100,.2,100), b=point(2,100.00001,.2,100.00001);
    audit::require(!policy::dominates(a,b),"distance and time tolerance");
    audit::require(policy::dominates(a,point(2,100.001,.2,100)),"strict improvement outside tolerance");
  });
  test("archive duplicates and rewards ignore Gini only in NoEQ",[&] {
    ParetoArchive ar(20); ar.tryAdd(point(2,100,.8,100));
    auto result=ar.tryAdd(point(2,100,.1,100));
    audit::require(result==(policy::equityEnabled?AddResult::DOMINATING:AddResult::IDENTICAL),"reward class");
    auto result2=ar.tryAdd(point(2,90,.9,90));
    audit::require(result2==(policy::equityEnabled?AddResult::NON_DOMINATED:AddResult::DOMINATING),"D/T reward class");
  });
  test("crowding and convergence invariant to Gini changes in NoEQ",[&] {
    ParetoArchive a(3), b(3);
    auto initial=point(3,300,.4,300); a.initializeReferenceBox(initial); b.initializeReferenceBox(initial);
    for(int i=0;i<12;++i) {
      a.tryAdd(point(2,100+i*5,.01*i,200-i*5));
      b.tryAdd(point(2,100+i*5,.9-.07*i,200-i*5));
    }
    if (!policy::equityEnabled) {
      const auto fa=a.getFront(), fb=b.getFront();
      audit::require(fa.size()==fb.size(),"archive size");
      for(size_t i=0;i<fa.size();++i) {
        audit::near(fa[i].getTotalDistance(),fb[i].getTotalDistance(),"pruning distance");
        audit::near(fa[i].getMaxTime(),fb[i].getMaxTime(),"pruning time");
      }
      audit::near(a.computeSearchHypervolume(),b.computeSearchHypervolume(),"search HV invariant");
    }
  });
  test("NoEQ convergence HV is analytically distance x MaxTime",[&] {
    ParetoArchive ar(20); ar.initializeReferenceBox(point(2,100,.3,200));
    ar.tryAdd(point(2,60,.2,90)); ar.tryAdd(point(2,90,.1,60));
    if (!policy::equityEnabled)
      audit::near(ar.computeSearchHypervolume(),(.7*.8)+(.5*.1),"2D union area");
    else audit::near(ar.computeSearchHypervolume(),ar.computeHypervolume2D(),"legacy convergence preserved");
  });
  test("weight projection inherits coefficients and removes zero channels",[&] {
    const auto& ws=policy::weights();
    audit::require(ws.size()==(policy::equityEnabled?10u:9u),"channel count");
    auto mixed=policy::project({.3,.6,.1});
    audit::near(mixed.dist,.3,"distance coefficient"); audit::near(mixed.time,.1,"time coefficient");
    for(auto w:ws) {
      audit::require(w.dist+w.gini+w.time>0,"no null channel");
      if (!policy::equityEnabled) audit::near(w.gini,0,"no equity channel");
    }
  });
  test("SA and segment scalarization Gini invariance",[&] {
    for(auto w:policy::weights()) {
      auto a=point(2,100,.9,100), b=point(2,100,.1,100);
      const double delta=w.gini*(policy::gini(a)-policy::gini(b));
      audit::near(delta,policy::equityEnabled?w.gini*.8:0.,"Gini contribution to scalarization");
    }
    auto mini=policy::project({.6,.2,.2});
    audit::near(mini.dist,.6,"mini SA distance inherited");
    audit::near(mini.time,.2,"mini SA time inherited");
  });
  test("adaptive insertion disables workload mode even for explicit equity hints",[&] {
    alns::AdaptiveInsertion op(nullptr); bool sawWorkload=false;
    for(bool vr : {false,true}) {
      op.setVehicleReductionMode(vr);
      for(const auto& hint : std::vector<policy::Weight>{{.33,.33,.34},{0,1,0},{.3,.6,.1}}) {
        op.setWeightHint(hint.dist,hint.gini,hint.time);
        for(int i=0;i<10000;++i) sawWorkload|=alns::SearchPolicyTestAccess::workload(op,i/10000.0);
      }
    }
    audit::require(sawWorkload==policy::equityEnabled,"workload mode availability");
  });
  test("optional Scatter Search diversity ignores Gini in NoEQ",[&] {
    std::vector<Solution> a,b;
    for(int i=0;i<7;++i) {
      a.push_back(point(2,100+10*i,.01*i,200-10*i));
      b.push_back(point(2,100+10*i,i==3?.9:.01*i,200-10*i));
    }
    auto ca=alns::SearchPolicyTestAccess::crowding(a), cb=alns::SearchPolicyTestAccess::crowding(b);
    if (!policy::equityEnabled) audit::require(ca==cb,"Scatter crowding invariant");
    else audit::require(ca!=cb,"full equity crowding retained");
  });
  test("true active-time Gini remains computed and reported",[&] {
    std::vector<std::shared_ptr<Node>> nodes{
      std::make_shared<Depot>(0,"D",0,0,0,1000),
      std::make_shared<Customer>(1,"C1",10,0,1,0,1000,0),
      std::make_shared<Customer>(2,"C2",30,0,1,0,1000,0)};
    auto inst=std::make_shared<Instance>(nodes,100,1000,1,1);
    Solution s(inst);
    for(int id=1;id<=2;++id) {
      Route r(id,std::make_shared<Vehicle>(id,100,1000,1),inst);
      r.addNode(id,1); r.evaluate(); s.addRoute(r);
    }
    s.evaluateRoutes(); audit::validateSolution(s,*inst);
    audit::near(s.getWorkloadGini(),.25,"real unequal-workload Gini");
    audit::near(policy::gini(s),policy::equityEnabled?.25:0,"search projection only");
  });
  std::cout<<passed<<" passed, "<<failed<<" failed\n";
  return failed?1:0;
}
