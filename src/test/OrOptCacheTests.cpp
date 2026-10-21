#include "ScheduleValidator.h"
#include "alns/LocalSearch.h"
#include "core/Customer.h"
#include "core/Depot.h"
#include <functional>
#include <iostream>
#include <random>

using Cache=alns::OrOptBlockCache;
using Mode=alns::OrOptCacheMode;
struct LocalSearchTestAccess {
  static void repeatExhausted(LocalSearch& search, Solution& solution) {
    search.searchContext_.invalidate(); search.updateSearchContext(solution);
    Cache::Session session(search.orOptCache_);
    audit::require(!search.searchOrOpt(solution,LocalSearchWeights{}),"fixture Or-opt block must be exhausted");
    const auto before=search.orOptCache_.stats;
    audit::require(!search.searchOrOpt(solution,LocalSearchWeights{}),"repeated Or-opt must still be exhausted");
    const auto after=search.orOptCache_.stats;
    audit::require(after.repeatedBlocks>0,"actual insertion blocks must repeat");
    if (search.orOptCache_.mode()==Mode::Observe)
      audit::require(after.skips==0 && after.candidatePositions==2*before.candidatePositions,"observe must recompute everything");
    else audit::require(after.skips>0 && after.candidatePositions<2*before.candidatePositions,"enabled must skip repeated enumeration");
  }
  static Cache::Key key(LocalSearch& search, int first, int second) {
    const auto& ctx=search.searchContext_;
    return {ctx.topologyEpoch,ctx.generations[first],ctx.generations[second],Cache::bits(search.distanceThreshold_),
            Cache::bits(1.0),first,second,1,1,3};
  }
  static void topologyAndGeneration(LocalSearch& search, Solution& solution) {
    search.searchContext_.invalidate(); search.updateSearchContext(solution);
    Cache::Session session(search.orOptCache_);
    const auto original=key(search,0,1); auto probe=search.orOptCache_.probe(original);
    search.orOptCache_.exhausted(original,probe);
    solution.getRoutes()[0].reverseNodes(1,3); solution.markDirty();
    search.searchContext_.markDirty(0); search.patchSearchContext(solution);
    audit::require(!search.orOptCache_.probe(key(search,0,1)).repeated,"reversal must invalidate same-centroid key");
    search.searchContext_.invalidate();
    std::swap(solution.getRoutes()[0],solution.getRoutes()[1]);
    search.updateSearchContext(solution);
    audit::require(!search.orOptCache_.probe(key(search,0,1)).repeated,"same-count reorder must invalidate epoch");
    const auto reordered=key(search,0,1);
    search.orOptCache_.exhausted(reordered,search.orOptCache_.probe(reordered));
    Route copied=solution.getRoutes()[1]; solution.removeRoute(1); solution.addRoute(copied);
    search.searchContext_.invalidate(); search.updateSearchContext(solution);
    audit::require(!search.orOptCache_.probe(key(search,0,1)).repeated,"remove/add same count must invalidate epoch");
  }
};
static Cache::Key key() { return {1,2,3,Cache::bits(50.0),Cache::bits(1.0),0,1,2,3,3}; }
static std::shared_ptr<Instance> instance(int count=6, bool random=false, unsigned seed=1) {
  std::mt19937 rng(seed);
  std::vector<std::shared_ptr<Node>> nodes{std::make_shared<Depot>(0,"D",0,0,0,1000)};
  for(int id=1;id<=count;++id) nodes.push_back(std::make_shared<Customer>(id,"C",random?rng()%40:10*id,random?rng()%40:0,1,0,1000,0));
  return std::make_shared<Instance>(nodes,3,1000,1,1);
}
static Route route(const std::shared_ptr<Instance>& inst, std::initializer_list<int> ids) {
  Route result(0,std::make_shared<Vehicle>(0,3,1000,1),inst); // Deliberately duplicate route ID.
  for(int id:ids) result.addNode(id);
  result.evaluate(); return result;
}
static Solution solution(const std::shared_ptr<Instance>& inst) {
  Solution result(inst); result.addRoute(route(inst,{1,2,3})); result.addRoute(route(inst,{4,5,6}));
  result.evaluateRoutes(); return result;
}
int main() {
  int passed=0,failed=0;
  auto test=[&](const char* name,const std::function<void()>& body) {
    try { body(); ++passed; std::cout<<"PASS "<<name<<'\n'; }
    catch(const std::exception& error) { ++failed; std::cout<<"FAIL "<<name<<": "<<error.what()<<'\n'; }
  };
  test("off and zero-capacity modes do not allocate a table or record hits",[] {
    for(auto mode:{Mode::Off,Mode::Observe,Mode::Enabled}) {
      Cache cache; cache.configure(mode,mode==Mode::Off?Cache::MaxEntries:0);
      Cache::Session session(cache);
      auto probe=cache.probe(key()); cache.exhausted(key(),probe);
      audit::require(!cache.active() && !cache.skip(probe) && cache.stats.lookups==0 && cache.bytes()==0,"disabled cache must be inert");
    }
  });
  test("observe records exact repeats without skipping",[] {
    Cache cache; cache.configure(Mode::Observe); Cache::Session session(cache);
    const auto input=key(); auto first=cache.probe(input);
    audit::require(!first.repeated,"fresh key"); cache.exhausted(input,first);
    const auto second=cache.probe(input);
    audit::require(second.repeated && !cache.skip(second) && cache.stats.repeatedBlocks==1,"observe repeats only");
  });
  test("every route scope and block field participates in key equality",[] {
    Cache cache; cache.configure(Mode::Enabled); Cache::Session session(cache);
    const auto original=key();
    std::vector<Cache::Key> changed(10,original);
    ++changed[0].epoch; ++changed[1].firstGeneration; ++changed[2].secondGeneration;
    changed[3].threshold=Cache::bits(51.0); changed[4].distanceWeight=Cache::bits(2.0);
    ++changed[5].firstRoute; ++changed[6].secondRoute; ++changed[7].position;
    ++changed[8].length; ++changed[9].topK;
    for(const auto& input:changed) {
      cache.exhausted(original,cache.probe(original));
      audit::require(!cache.probe(input).repeated,"different scope must not reuse exhausted result");
    }
    audit::require(cache.skip(cache.probe(original)),"unchanged key remains eligible");
  });
  test("hash collisions evict entries but cannot create false hits",[] {
    Cache cache; cache.configure(Mode::Enabled,1); Cache::Session session(cache);
    const auto first=key(); auto second=first; ++second.position;
    cache.exhausted(first,cache.probe(first));
    audit::require(!cache.probe(second).repeated,"collision must compare full key");
    cache.exhausted(second,cache.probe(second));
    audit::require(!cache.probe(first).repeated && cache.skip(cache.probe(second)),"collision eviction is a miss");
    audit::require(cache.stats.evictions==1 && cache.stats.peakEntries==1,"bounded occupancy");
  });
  test("session exit exception and reconfiguration invalidate old records",[] {
    Cache cache; cache.configure(Mode::Enabled);
    Cache::Probe stale;
    try {
      Cache::Session session(cache); stale=cache.probe(key()); cache.exhausted(key(),stale);
      throw std::runtime_error("unwind");
    } catch(const std::runtime_error&) {}
    audit::require(!cache.active(),"RAII session unwinding");
    { Cache::Session session(cache);
      audit::require(!cache.probe(key()).repeated && !cache.skip(stale),"old session is invalid");
    }
    cache.configure(Mode::Enabled);
    { Cache::Session session(cache);
      audit::require(!cache.probe(key()).repeated && !cache.skip(stale),"configure clears table and invalidates old tokens");
    }
  });
  test("unfinished successful and interrupted blocks are not cached as exhausted",[] {
    Cache cache; cache.configure(Mode::Observe); Cache::Session session(cache);
    auto probe=cache.probe(key()); cache.improved(probe);
    audit::require(!cache.probe(key()).repeated,"success is not failure memoization");
    cache.exhausted(key(),cache.probe(key()));
    bool detected=false;
    try { cache.improved(cache.probe(key())); } catch(const std::logic_error&) { detected=true; }
    audit::require(detected && cache.stats.mismatches==1,"observe must detect inconsistent exhaustion");
  });
  test("actual Or-opt repeat keeps observe and enabled behavior equal",[] {
    auto inst=instance();
    for(auto mode:{Mode::Observe,Mode::Enabled}) {
      auto input=solution(inst); const auto original=input.getHash();
      LocalSearch search(inst); search.configureOrOptCache(mode);
      LocalSearchTestAccess::repeatExhausted(search,input);
      input.evaluateRoutes(); audit::validateSolution(input,*inst);
      audit::require(input.getHash()==original,"exhausted search must preserve routes");
    }
  });
  test("actual reversal and same-count topology changes invalidate block keys",[] {
    auto inst=instance(); auto input=solution(inst);
    LocalSearch search(inst); search.configureOrOptCache(Mode::Observe);
    LocalSearchTestAccess::topologyAndGeneration(search,input);
    input.evaluateRoutes(); audit::validateSolution(input,*inst);
  });
  test("full local search off observe and enabled preserve feasible route sequences",[] {
    for(unsigned seed=1;seed<=16;++seed) {
      auto inst=instance(6,true,seed); const auto original=solution(inst);
      auto reference=original; LocalSearch off(inst); off.run(reference); reference.evaluateRoutes();
      audit::validateSolution(reference,*inst);
      for(auto mode:{Mode::Observe,Mode::Enabled}) {
        auto input=original; LocalSearch search(inst); search.configureOrOptCache(mode);
        search.run(input); input.evaluateRoutes(); audit::validateSolution(input,*inst);
        audit::require(input.getHash()==reference.getHash(),"cache mode changed accepted-move outcome");
        audit::near(input.getTotalDistance(),reference.getTotalDistance(),"cache distance");
        audit::require(search.getOrOptCacheStatistics().mismatches==0,"no false exhaustion hits");
      }
    }
  });
  test("capacity is capped and no table lives on the LocalSearch stack",[] {
    Cache cache; cache.configure(Mode::Observe,Cache::MaxEntries+100);
    audit::require(cache.capacity()==4096 && cache.bytes()<=1024*1024,"bounded cache memory");
    audit::require(sizeof(Cache)<1024,"table storage must be heap-owned");
    std::cout<<"cache_capacity="<<cache.capacity()<<" table_bytes="<<cache.bytes()<<'\n';
  });
  std::cout<<"RESULT passed="<<passed<<" failed="<<failed<<'\n';
  return failed?1:0;
}
