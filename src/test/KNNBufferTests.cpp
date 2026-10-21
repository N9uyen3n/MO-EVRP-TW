#include "ScheduleValidator.h"
#include "alns/LocalSearch.h"
#include "core/Customer.h"
#include "core/Depot.h"
#include "core/Station.h"
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <new>

namespace {
bool countAllocations=false;
std::size_t allocations=0;
}
void* operator new(std::size_t size) {
  if (countAllocations) ++allocations;
  if (void* pointer=std::malloc(size?size:1)) return pointer;
  throw std::bad_alloc();
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void* pointer) noexcept { ::operator delete(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { ::operator delete(pointer); }

struct LocalSearchTestAccess {
  static void positions(LocalSearch& search, const Route& route, int node, int topK,
                        std::vector<std::size_t>& output) {
    search.findBestInsertionPositions_KNN(route,node,topK,output);
  }
  static std::vector<std::size_t> fallback(LocalSearch& search, const Route& route, int node, int topK) {
    return search.findBestInsertionPositions_TimeAware(route,node,topK);
  }
};
static std::shared_ptr<Instance> instance() {
  std::vector<std::shared_ptr<Node>> nodes{std::make_shared<Depot>(0,"D",0,0,0,1000)};
  for (int id=1;id<=4;++id) nodes.push_back(std::make_shared<Customer>(id,"C",10*id,id==4?1:0,1,0,1000,0));
  nodes.push_back(std::make_shared<Station>(5,"S",5,0,0,1000,1));
  return std::make_shared<Instance>(nodes,100,1000,1,1);
}
static Route route(const std::shared_ptr<Instance>& inst, std::initializer_list<int> ids) {
  Route result(0,std::make_shared<Vehicle>(0,100,1000,1),inst);
  for (int id:ids) result.addNode(id);
  result.evaluate(); return result;
}
int main() {
  int passed=0,failed=0;
  auto test=[&](const char* name, const std::function<void()>& body) {
    try { body(); ++passed; std::cout<<"PASS "<<name<<'\n'; }
    catch (const std::exception& error) { countAllocations=false; ++failed; std::cout<<"FAIL "<<name<<": "<<error.what()<<'\n'; }
  };
  test("KNN returns the unique shortest insertion and all legal positions",[] {
    auto inst=instance(); LocalSearch search(inst); auto input=route(inst,{1,2,3});
    std::vector<std::size_t> output{999};
    LocalSearchTestAccess::positions(search,input,4,1,output);
    audit::require(output==std::vector<std::size_t>{4},"append before depot has unique minimum detour");
    LocalSearchTestAccess::positions(search,input,4,20,output);
    audit::require(output.size()==4,"top-K clamped to route positions");
    std::sort(output.begin(),output.end());
    audit::require(output==std::vector<std::size_t>({1,2,3,4}),"legal unique positions only");
  });
  test("zero top-K clears stale caller output and preserves capacity",[] {
    auto inst=instance(); LocalSearch search(inst); auto input=route(inst,{1,2});
    std::vector<std::size_t> output{91,92}; const auto capacity=output.capacity();
    LocalSearchTestAccess::positions(search,input,3,0,output);
    audit::require(output.empty() && output.capacity()==capacity,"zero top-K must clear without allocating");
  });
  test("depot-only and one-customer routes return valid short outputs",[] {
    auto inst=instance(); LocalSearch search(inst); std::vector<std::size_t> output;
    auto empty=route(inst,{}); LocalSearchTestAccess::positions(search,empty,4,3,output);
    audit::require(output==std::vector<std::size_t>{1},"empty route insertion position");
    auto one=route(inst,{1}); LocalSearchTestAccess::positions(search,one,4,3,output);
    std::sort(output.begin(),output.end());
    audit::require(output==std::vector<std::size_t>({1,2}),"one-customer positions");
  });
  test("no KNN customer in route falls back to time-aware insertion",[] {
    auto inst=instance(); LocalSearch search(inst); auto input=route(inst,{5});
    std::vector<std::size_t> output;
    LocalSearchTestAccess::positions(search,input,4,3,output);
    audit::require(output==LocalSearchTestAccess::fallback(search,input,4,3),"fallback ordering");
    audit::require(output.size()==2,"station-only route positions");
  });
  test("station without a KNN entry uses the same owned output contract",[] {
    auto inst=instance(); LocalSearch search(inst); auto input=route(inst,{1,2});
    std::vector<std::size_t> output;
    LocalSearchTestAccess::positions(search,input,5,2,output);
    audit::require(output==LocalSearchTestAccess::fallback(search,input,5,2),"non-KNN node fallback");
  });
  test("separate outputs survive interleaved calls and separate search objects",[] {
    auto inst=instance(); LocalSearch first(inst),second(inst);
    auto a=route(inst,{1,2,3}),b=route(inst,{5});
    std::vector<std::size_t> left,right;
    LocalSearchTestAccess::positions(first,a,4,4,left); const auto saved=left;
    LocalSearchTestAccess::positions(first,b,4,2,right);
    audit::require(left==saved,"same object must not overwrite caller output");
    LocalSearchTestAccess::positions(second,b,4,1,right);
    audit::require(left==saved,"other object must not overwrite caller output");
  });
  test("warmed KNN and both fallback paths allocate no output per call",[] {
    auto inst=instance(); LocalSearch search(inst);
    auto normal=route(inst,{1,2,3}),fallback=route(inst,{5});
    std::vector<std::size_t> output; output.reserve(8);
    for (int i=0;i<8;++i) {
      LocalSearchTestAccess::positions(search,normal,4,4,output);
      LocalSearchTestAccess::positions(search,fallback,4,3,output);
      LocalSearchTestAccess::positions(search,normal,5,3,output);
    }
    allocations=0; countAllocations=true;
    std::size_t checksum=0;
    for (int i=0;i<128;++i) {
      LocalSearchTestAccess::positions(search,normal,4,4,output); checksum+=output.size();
      LocalSearchTestAccess::positions(search,fallback,4,3,output); checksum+=output.size();
      LocalSearchTestAccess::positions(search,normal,5,3,output); checksum+=output.size();
    }
    countAllocations=false;
    std::cout<<"warmed_calls=384 allocations="<<allocations<<" checksum="<<checksum<<'\n';
    audit::require(allocations==0 && checksum==128*9,"warmed calls must reuse capacity in all paths");
  });
  std::cout<<"RESULT passed="<<passed<<" failed="<<failed<<'\n';
  return failed?1:0;
}
