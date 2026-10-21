#include "ScheduleValidator.h"
#include "alns/LocalSearch.h"
#include "core/Customer.h"
#include "core/Depot.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <numeric>
#include <random>

struct LocalSearchTestAccess {
  static void init(LocalSearch& search, const Solution& solution) {
    search.searchContext_.invalidate();
    search.updateSearchContext(solution);
    for (std::size_t r=0; r<solution.getNumRoutes(); ++r)
      search.getCachedRanking(static_cast<int>(r), solution.getRoutes()[r], search.searchContext_);
  }
  static void dirty(LocalSearch& search, int first, int second=-1) {
    search.searchContext_.markDirty(first, second);
  }
  static bool anyDirty(const LocalSearch& search) {
    return std::find(search.searchContext_.centroidDirty.begin(),
                     search.searchContext_.centroidDirty.end(), true)
            != search.searchContext_.centroidDirty.end();
  }
  static bool rankingDirty(const LocalSearch& search, int route) {
    return search.searchContext_.rankingDirty.at(route);
  }
  static std::uint64_t generation(const LocalSearch& search, int route) {
    return search.searchContext_.generations.at(route);
  }
  static void patch(LocalSearch& search, const Solution& solution) {
    search.patchSearchContext(solution);
  }
  static void invalidate(LocalSearch& search) { search.searchContext_.invalidate(); }
  static std::vector<int> neighbors(const LocalSearch& search, int route) {
    return search.searchContext_.neighborLists.at(route);
  }
  static bool interTwoOpt(LocalSearch& search, Solution& solution) {
    return search.searchInterTwoOpt(solution, LocalSearchWeights{}, search.searchContext_);
  }
  static bool reversed(LocalSearch& search, Solution& solution) {
    return search.searchOrOptReversed(solution, LocalSearchWeights{}, search.searchContext_);
  }
  static MoveEvaluation swap(LocalSearch& search, Solution& solution) {
    MoveDescriptor move;
    move.type=MoveType::INTER_SWAP; move.routeIdx1=9; move.routeIdx2=10;
    move.nodeIdx1=move.nodeIdx2=1;
    search.evaluateMove(solution, move, LocalSearchWeights{});
    audit::require(move.eval.isFeasible && move.eval.distanceDelta<0, "fixture move improves feasibly");
    search.applyMove(solution, move);
    return move.eval;
  }
  static void check(LocalSearch& search, const Solution& solution) {
    const auto patched=search.searchContext_;
    search.searchContext_.invalidate(); search.updateSearchContext(solution);
    audit::require(patched.neighborLists==search.searchContext_.neighborLists, "patch differs from full rebuild");
    search.searchContext_=patched;
    const auto& routes=solution.getRoutes();
    audit::require(patched.centroids.size()==routes.size(), "centroid count");
    for (std::size_t r=0; r<routes.size(); ++r) {
      audit::near(patched.centroids[r].x, routes[r].getCentroidX(), "centroid x");
      audit::near(patched.centroids[r].y, routes[r].getCentroidY(), "centroid y");
      std::vector<std::pair<double,int>> ranked;
      for (std::size_t other=0; other<routes.size(); ++other) if (other!=r) {
        const double dx=routes[r].getCentroidX()-routes[other].getCentroidX();
        const double dy=routes[r].getCentroidY()-routes[other].getCentroidY();
        ranked.emplace_back(std::fma(dx,dx,dy*dy), static_cast<int>(other));
      }
      std::sort(ranked.begin(), ranked.end());
      std::vector<int> expected{static_cast<int>(r)};
      for (std::size_t i=0; i<std::min<std::size_t>(8, ranked.size()); ++i)
        expected.push_back(ranked[i].second);
      audit::require(patched.neighborLists.at(r)==expected,
                     "ordered neighbors disagree with independent oracle at route "+std::to_string(r));
      audit::require(!patched.centroidDirty[r], "centroid dirty after patch");
      if (patched.rankingDirty[r]) {
        const auto& actual=search.getCachedRanking(static_cast<int>(r), routes[r], search.searchContext_);
        audit::require(actual==search.rankNodesByRemovalSavings(routes[r]), "dirty ranking not refreshed");
      }
    }
    search.searchContext_.invalidate(); search.updateSearchContext(solution);
    audit::require(patched.neighborLists==search.searchContext_.neighborLists, "patch differs from full rebuild");
    // Preserve the incremental context, including the lazy ranking flags, for the next mutation.
    search.searchContext_=patched;
  }
};

static std::shared_ptr<Instance> instance(const std::vector<std::pair<double,double>>& points) {
  std::vector<std::shared_ptr<Node>> nodes{std::make_shared<Depot>(0,"D",0,0,0,100000)};
  for (std::size_t i=0; i<points.size(); ++i)
    nodes.push_back(std::make_shared<Customer>(static_cast<int>(i+1),"C",points[i].first,points[i].second,1,0,100000,0));
  return std::make_shared<Instance>(nodes,100,100000,1,1);
}
static Route route(const std::shared_ptr<Instance>& inst, const std::vector<int>& ids) {
  Route result(0,std::make_shared<Vehicle>(0,100,100000,1),inst);
  for (int id:ids) result.addNode(id);
  result.evaluate(); return result;
}
static void exchange(Solution& solution, int first, int p, int second, int q) {
  auto& routes=solution.getRoutes();
  const int a=routes[first].getNodeAt(p), b=routes[second].getNodeAt(q);
  routes[first].removeNode(p); routes[first].addNode(b,p);
  routes[second].removeNode(q); routes[second].addNode(a,q);
  solution.markDirty();
}

int main() {
  int passed=0, failed=0;
  auto test=[&](const char* name, const std::function<void()>& body) {
    try { body(); ++passed; std::cout<<"PASS "<<name<<'\n'; }
    catch (const std::exception& error) { ++failed; std::cout<<"FAIL "<<name<<": "<<error.what()<<'\n'; }
  };
  test("newly close dirty route enters a clean route top eight",[] {
    auto inst=instance({{0,0},{10,0},{20,0},{30,0},{40,0},{50,0},{60,0},{70,0},{80,0},{1000,0},{1,0},{2001,0}});
    Solution solution(inst);
    for (int r=0;r<11;++r) solution.addRoute(route(inst,r==10?std::vector<int>{11,12}:std::vector<int>{r+1}));
    solution.evaluateRoutes(); audit::validateSolution(solution,*inst);
    LocalSearch search(inst); LocalSearchTestAccess::init(search,solution);
    const double before=solution.getTotalDistance();
    const auto move=LocalSearchTestAccess::swap(search,solution);
    solution.evaluateRoutes(); audit::validateSolution(solution,*inst);
    audit::near(move.distanceDelta,solution.getTotalDistance()-before,"accepted swap distance");
    LocalSearchTestAccess::patch(search,solution);
    const auto neighbors=LocalSearchTestAccess::neighbors(search,0);
    audit::require(std::find(neighbors.begin(),neighbors.end(),9)!=neighbors.end(),"newly close route omitted");
    LocalSearchTestAccess::check(search,solution);
  });
  test("ties and coincident centroids have a deterministic index order",[] {
    auto inst=instance(std::vector<std::pair<double,double>>(13,{0,0}));
    Solution solution(inst); for (int id=1;id<=13;++id) solution.addRoute(route(inst,{id}));
    solution.evaluateRoutes(); LocalSearch search(inst); LocalSearchTestAccess::init(search,solution);
    LocalSearchTestAccess::check(search,solution);
    LocalSearchTestAccess::dirty(search,10,12); LocalSearchTestAccess::patch(search,solution);
    LocalSearchTestAccess::check(search,solution);
  });
  test("new dirty neighbor at equal cutoff distance uses the index tie-break",[] {
    auto inst=instance({{0,0},{1000,0},{10,0},{20,0},{30,0},{40,0},{50,0},{60,0},{70,0},{80,0},{80,0}});
    Solution solution(inst); for (int id=1;id<=11;++id) solution.addRoute(route(inst,{id}));
    solution.evaluateRoutes(); LocalSearch search(inst); LocalSearchTestAccess::init(search,solution);
    exchange(solution,1,1,10,1); LocalSearchTestAccess::dirty(search,1,10);
    LocalSearchTestAccess::patch(search,solution); LocalSearchTestAccess::check(search,solution);
    const auto neighbors=LocalSearchTestAccess::neighbors(search,0);
    audit::require(neighbors.back()==1,"lower index must enter at equal cutoff distance");
    audit::require(LocalSearchTestAccess::rankingDirty(search,1),"changed route ranking dirty");
    audit::require(!LocalSearchTestAccess::rankingDirty(search,0),"clean ranking must remain cached");
    solution.evaluateRoutes(); audit::validateSolution(solution,*inst);
  });
  test("zero one few and depot-only intermediate routes are supported",[] {
    auto inst=instance({{1,0},{2,0},{3,0},{4,0}});
    for (int count=0;count<=5;++count) {
      Solution solution(inst);
      for (int r=0;r<count;++r) solution.addRoute(route(inst,r==4?std::vector<int>{}:std::vector<int>{r+1}));
      LocalSearch search(inst); LocalSearchTestAccess::init(search,solution);
      LocalSearchTestAccess::check(search,solution);
      LocalSearchTestAccess::dirty(search,count-1); LocalSearchTestAccess::patch(search,solution);
      LocalSearchTestAccess::check(search,solution);
    }
  });
  test("route count changes rebuild and same-count topology explicitly invalidates",[] {
    auto inst=instance({{1,2},{5,3},{10,7},{20,3}});
    Solution solution(inst); for (int id=1;id<=4;++id) solution.addRoute(route(inst,{id}));
    LocalSearch search(inst); LocalSearchTestAccess::init(search,solution);
    solution.removeRoute(1); LocalSearchTestAccess::patch(search,solution);
    LocalSearchTestAccess::check(search,solution);
    solution.addRoute(route(inst,{2})); LocalSearchTestAccess::patch(search,solution);
    LocalSearchTestAccess::check(search,solution);
    std::swap(solution.getRoutes()[0],solution.getRoutes()[3]);
    LocalSearchTestAccess::invalidate(search); LocalSearchTestAccess::patch(search,solution);
    LocalSearchTestAccess::check(search,solution);
    solution.evaluateRoutes(); audit::validateSolution(solution,*inst);
  });
  test("sequence changes invalidate ranking even when centroid does not change",[] {
    auto inst=instance({{1,0},{2,1},{3,0},{9,0}});
    Solution solution(inst); solution.addRoute(route(inst,{1,2,3})); solution.addRoute(route(inst,{4}));
    LocalSearch search(inst); search.configureOrOptCache(alns::OrOptCacheMode::Observe);
    LocalSearchTestAccess::init(search,solution);
    const auto generation=LocalSearchTestAccess::generation(search,0);
    solution.getRoutes()[0].reverseNodes(1,3); solution.markDirty();
    LocalSearchTestAccess::dirty(search,0); LocalSearchTestAccess::patch(search,solution);
    audit::require(LocalSearchTestAccess::rankingDirty(search,0),"reversal must invalidate removal ranking");
    audit::require(LocalSearchTestAccess::generation(search,0)==generation+1,"unchanged centroid still advances generation");
    LocalSearchTestAccess::check(search,solution);
    solution.evaluateRoutes(); audit::validateSolution(solution,*inst);
  });
  test("random Euclidean swaps and reversals match an independent full oracle",[] {
    std::mt19937 rng(1729);
    for (int trial=0;trial<32;++trial) {
      const int count=1+static_cast<int>(rng()%20);
      std::vector<std::pair<double,double>> points;
      for (int i=0;i<3*count;++i) points.emplace_back(static_cast<int>(rng()%30),static_cast<int>(rng()%30));
      auto inst=instance(points); Solution solution(inst);
      for (int r=0;r<count;++r) solution.addRoute(route(inst,{3*r+1,3*r+2,3*r+3}));
      solution.evaluateRoutes(); LocalSearch search(inst); LocalSearchTestAccess::init(search,solution);
      for (int step=0;step<40;++step) {
        const int first=rng()%count, second=rng()%count;
        if (first==second) {
          solution.getRoutes()[first].reverseNodes(1,3); solution.markDirty();
        } else exchange(solution,first,1+rng()%3,second,1+rng()%3);
        LocalSearchTestAccess::dirty(search,first,second);
        LocalSearchTestAccess::patch(search,solution);
        LocalSearchTestAccess::check(search,solution);
        solution.evaluateRoutes(); audit::validateSolution(solution,*inst);
      }
    }
  });
  test("direct inter-two-opt commits dirty routes before patch",[] {
    auto inst=instance({{1,0},{100,0},{100,1},{1,1}});
    Solution solution(inst); solution.addRoute(route(inst,{1,2})); solution.addRoute(route(inst,{3,4}));
    solution.evaluateRoutes(); LocalSearch search(inst); search.configureOrOptCache(alns::OrOptCacheMode::Observe);
    LocalSearchTestAccess::init(search,solution);
    audit::require(LocalSearchTestAccess::interTwoOpt(search,solution),"fixture must improve");
    audit::require(LocalSearchTestAccess::anyDirty(search),"direct commit must mark context dirty");
    audit::require(LocalSearchTestAccess::generation(search,0)==1 && LocalSearchTestAccess::generation(search,1)==1,
                   "direct inter-two-opt must advance both route generations");
    LocalSearchTestAccess::patch(search,solution); LocalSearchTestAccess::check(search,solution);
    solution.evaluateRoutes(); audit::validateSolution(solution,*inst);
  });
  test("direct reversed-or-opt commits dirty routes before patch",[] {
    std::mt19937 rng(834);
    int improvements=0;
    for (int trial=0;trial<12;++trial) {
      std::vector<std::pair<double,double>> points;
      for (int id=1;id<=12;++id) points.emplace_back(rng()%40,rng()%40);
      auto inst=instance(points); Solution solution(inst);
      solution.addRoute(route(inst,{1,2,3,4})); solution.addRoute(route(inst,{5,6,7,8})); solution.addRoute(route(inst,{9,10,11,12}));
      solution.evaluateRoutes(); LocalSearch search(inst); search.configureOrOptCache(alns::OrOptCacheMode::Observe);
      LocalSearchTestAccess::init(search,solution);
      if (LocalSearchTestAccess::reversed(search,solution)) {
        ++improvements;
        audit::require(LocalSearchTestAccess::anyDirty(search),"direct reversed commit dirty flag");
        audit::require(LocalSearchTestAccess::generation(search,0)+LocalSearchTestAccess::generation(search,1)+
                       LocalSearchTestAccess::generation(search,2)>0,"direct reversed commit generation");
        LocalSearchTestAccess::patch(search,solution); LocalSearchTestAccess::check(search,solution);
        solution.evaluateRoutes(); audit::validateSolution(solution,*inst);
      }
    }
    audit::require(improvements>0,"reversed operator must be exercised");
  });
  std::cout<<"RESULT passed="<<passed<<" failed="<<failed<<'\n';
  return failed?1:0;
}
