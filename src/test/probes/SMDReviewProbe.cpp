// Diagnostic probe for the review at 407a32b, not a production CTest target.
// Confirms the current context-patching limitation and measures warmed KNN
// allocations in one thread. After a context fix, replace the stale-list
// expectation with a regression requiring equality to the full rebuild.
// Link to the matching Release library with ALNS_INSERTION_TIME_BOUNDS=1.
#include "alns/LocalSearch.h"
#include "ScheduleValidator.h"
#include "core/Depot.h"
#include <algorithm>
#include <iostream>
#include <type_traits>
#include <cstdlib>
#include <new>

static bool countAllocations=false;
static std::size_t allocationCalls=0;
void* operator new(std::size_t size) {
  if(countAllocations) ++allocationCalls;
  if(void* p=std::malloc(size?size:1)) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete[](void* p,std::size_t) noexcept { ::operator delete(p); }

struct LocalSearchTestAccess {
  static bool knnReturnsReference() {
    return std::is_reference<decltype(std::declval<LocalSearch>().findBestInsertionPositions_KNN(std::declval<const Route&>(),1,4))>::value;
  }
  static std::size_t consumePositions(LocalSearch& ls,const Route& route) {
    const auto& positions=ls.findBestInsertionPositions_KNN(route,2,4);
    std::size_t result=positions.size();
    for(auto p:positions) result+=p;
    return result;
  }
  static void init(LocalSearch& ls,const Solution& solution) { ls.updateSearchContext(solution); }
  static std::vector<int> neighbors(LocalSearch& ls,int r) { return ls.searchContext_.neighborLists[r]; }
  static MoveEvaluation swap(LocalSearch& ls,Solution& solution) {
    MoveDescriptor move;
    move.type=MoveType::INTER_SWAP;
    move.routeIdx1=9; move.routeIdx2=10; move.nodeIdx1=move.nodeIdx2=1;
    ls.evaluateMove(solution,move,LocalSearchWeights{});
    audit::require(move.eval.isFeasible && move.eval.distanceDelta<0,"swap must be feasible and improve distance");
    ls.applyMove(solution,move);
    return move.eval;
  }
  static void patch(LocalSearch& ls,const Solution& solution) { ls.patchSearchContext(solution); }
  static void rebuild(LocalSearch& ls,const Solution& solution) {
    ls.searchContext_.invalidate(); ls.updateSearchContext(solution);
  }
};
static void print(const char* label,const std::vector<int>& ids) {
  std::cout<<label;
  for(int id:ids) std::cout<<' '<<id;
  std::cout<<'\n';
}
int main() {
  try {
    std::cout<<"MoveDescriptor bytes="<<sizeof(MoveDescriptor)
      <<" trivially_copyable="<<std::is_trivially_copyable<MoveDescriptor>::value<<'\n';
    std::vector<std::shared_ptr<Node>> nodes{std::make_shared<Depot>(0,"D",0,0,0,100000)};
    const double xs[]={0,10,20,30,40,50,60,70,80,1000,1,2001};
    for(int i=0;i<12;++i)
      nodes.push_back(std::make_shared<Customer>(i+1,"C",xs[i],0,1,0,100000,0));
    auto instance=std::make_shared<Instance>(nodes,100,100000,1,1);
    auto vehicle=std::make_shared<Vehicle>(0,100,100000,1);
    Solution solution(instance);
    for(int r=0;r<11;++r) {
      Route route(r,vehicle,instance);
      route.addNode(r+1);
      if(r==10) route.addNode(12);
      solution.addRoute(route);
    }
    solution.evaluateRoutes(); audit::validateSolution(solution,*instance);
    const double before=solution.getTotalDistance();
    LocalSearch search(instance); LocalSearchTestAccess::init(search,solution);
    const Solution& readOnly=solution;
    for(int i=0;i<8;++i) LocalSearchTestAccess::consumePositions(search,readOnly.getRoutes()[0]);
    allocationCalls=0; countAllocations=true;
    std::size_t checksum=0;
    for(int i=0;i<128;++i) checksum+=LocalSearchTestAccess::consumePositions(search,readOnly.getRoutes()[0]);
    countAllocations=false;
    std::cout<<"knn_returns_reference="<<LocalSearchTestAccess::knnReturnsReference()
      <<" warmed_calls=128 allocation_calls="<<allocationCalls<<" checksum="<<checksum<<'\n';
    const auto original=LocalSearchTestAccess::neighbors(search,0);
    print("neighbors_before",original);
    const auto evaluation=LocalSearchTestAccess::swap(search,solution);
    solution.evaluateRoutes(); audit::validateSolution(solution,*instance);
    audit::near(solution.getTotalDistance()-before,evaluation.distanceDelta,"swap actual distance delta");
    std::cout<<"valid_before_and_after=1 distance_delta="<<evaluation.distanceDelta<<'\n';
    LocalSearchTestAccess::patch(search,solution);
    const auto patched=LocalSearchTestAccess::neighbors(search,0);
    LocalSearchTestAccess::rebuild(search,solution);
    const auto rebuilt=LocalSearchTestAccess::neighbors(search,0);
    print("neighbors_after_patch",patched);
    print("neighbors_full_rebuild",rebuilt);
    audit::require(std::find(patched.begin(),patched.end(),9)==patched.end(),"expected stale list missing route 9");
    audit::require(std::find(rebuilt.begin(),rebuilt.end(),9)!=rebuilt.end(),"full rebuild should contain newly close route 9");
    std::cout<<"CONFIRMED newly close dirty route omitted by patch; all routes remain feasible\n";
    return 0;
  } catch(const std::exception& e) {
    std::cerr<<e.what()<<'\n'; return 1;
  }
}
