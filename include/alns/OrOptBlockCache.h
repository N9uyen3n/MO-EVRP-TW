#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace alns {
enum class OrOptCacheMode { Off=0, Observe=1, Enabled=2 };
struct OrOptCacheStatistics {
  std::uint64_t sessions=0, lookups=0, repeatedBlocks=0, skips=0;
  std::uint64_t inserts=0, evictions=0, peakEntries=0, exhaustedBlocks=0, successfulBlocks=0;
  std::uint64_t candidatePositions=0, repeatedCandidatePositions=0, evaluations=0, repeatedEvaluations=0;
  std::uint64_t mismatches=0, generationChanges=0, topologyRebuilds=0;
  std::uint64_t capacity=0, bytes=0;
};

// Records exhaustion of one ordered Or-opt insertion block, NOT mathematical
// infeasibility. Instance/vehicle/config data must remain fixed in a session;
// committed route changes are represented by topology epoch and generations.
// Direct mapping bounds memory and needs no per-probe allocation. Collisions
// lose hits only: the entire key is compared before a block can be skipped.
class OrOptBlockCache {
public:
  static constexpr std::size_t MaxEntries=4096;
  struct Key {
    std::uint64_t epoch=0, firstGeneration=0, secondGeneration=0;
    std::uint64_t threshold=0, distanceWeight=0;
    int firstRoute=0, secondRoute=0, position=0, length=0, topK=3;
    bool operator==(const Key& other) const {
      return epoch==other.epoch && firstGeneration==other.firstGeneration &&
        secondGeneration==other.secondGeneration && threshold==other.threshold &&
        distanceWeight==other.distanceWeight && firstRoute==other.firstRoute &&
        secondRoute==other.secondRoute && position==other.position &&
        length==other.length && topK==other.topK;
    }
  };
  struct Probe { std::size_t slot=0; bool repeated=false; std::uint64_t session=0; };
  static std::uint64_t bits(double value) {
    std::uint64_t result;
    static_assert(sizeof(result)==sizeof(value), "double key representation");
    std::memcpy(&result,&value,sizeof(result)); return result;
  }
  void configure(OrOptCacheMode mode, std::size_t capacity=MaxEntries) {
    if (inSession_) throw std::logic_error("Configure Or-opt cache between sessions");
    // Keep the session stamp monotonic across reconfiguration too: a token
    // obtained before configure() must never alias the first new session.
    mode_=mode; capacity_=std::min(capacity,MaxEntries); stats={};
    entries_.reset();
    if (mode_!=OrOptCacheMode::Off && capacity_) entries_=std::make_unique<Entry[]>(capacity_);
    stats.capacity=capacity_; stats.bytes=bytes();
  }
  bool active() const { return inSession_ && entries_ && mode_!=OrOptCacheMode::Off; }
  OrOptCacheMode mode() const { return mode_; }
  std::size_t capacity() const { return capacity_; }
  std::size_t bytes() const { return entries_ ? capacity_*sizeof(Entry) : 0; }
  Probe probe(const Key& key) {
    if (!active()) return {};
    ++stats.lookups;
    std::uint64_t hash=0x9e3779b97f4a7c15ULL;
    auto mix=[&](std::uint64_t value) { hash^=value+0x9e3779b97f4a7c15ULL+(hash<<6)+(hash>>2); };
    mix(key.epoch); mix(key.firstGeneration); mix(key.secondGeneration);
    mix(key.threshold); mix(key.distanceWeight);
    mix(static_cast<std::uint64_t>(key.firstRoute)); mix(static_cast<std::uint64_t>(key.secondRoute));
    mix(static_cast<std::uint64_t>(key.position)); mix(static_cast<std::uint64_t>(key.length)); mix(key.topK);
    const std::size_t slot=hash%capacity_;
    const Entry& entry=entries_[slot];
    const bool repeated=entry.stamp==stamp_ && entry.key==key;
    if (repeated) ++stats.repeatedBlocks;
    return {slot,repeated,stamp_};
  }
  bool skip(const Probe& probe) {
    if (!active() || probe.session!=stamp_ || !probe.repeated || mode_!=OrOptCacheMode::Enabled) return false;
    ++stats.skips; return true;
  }
  void exhausted(const Key& key, const Probe& probe) {
    if (!active() || probe.session!=stamp_ || probe.slot>=capacity_) return;
    ++stats.exhaustedBlocks;
    Entry& entry=entries_[probe.slot];
    if (entry.stamp==stamp_ && entry.key==key) return;
    if (entry.stamp==stamp_) ++stats.evictions;
    else { ++live_; stats.peakEntries=std::max<std::uint64_t>(stats.peakEntries,live_); }
    entry.key=key; entry.stamp=stamp_; ++stats.inserts;
  }
  void improved(const Probe& probe) {
    if (!active() || probe.session!=stamp_) return;
    ++stats.successfulBlocks;
    if (probe.repeated) {
      ++stats.mismatches;
      throw std::logic_error("Repeated exhausted Or-opt block found an improvement");
    }
  }
  class Session {
  public:
    explicit Session(OrOptBlockCache& owner):owner_(owner) {
      if (owner_.inSession_) throw std::logic_error("Nested Or-opt cache session");
      owner_.inSession_=true; owner_.live_=0;
      if (owner_.active()) {
        if (++owner_.stamp_==0) {
          for (std::size_t i=0;i<owner_.capacity_;++i) owner_.entries_[i].stamp=0;
          owner_.stamp_=1;
        }
        ++owner_.stats.sessions;
      }
    }
    ~Session() { owner_.inSession_=false; }
    Session(const Session&)=delete;
    Session& operator=(const Session&)=delete;
  private:
    OrOptBlockCache& owner_;
  };
  OrOptCacheStatistics stats;
private:
  struct Entry { Key key; std::uint64_t stamp=0; };
  OrOptCacheMode mode_=OrOptCacheMode::Off;
  std::size_t capacity_=MaxEntries, live_=0;
  std::uint64_t stamp_=0;
  bool inSession_=false;
  std::unique_ptr<Entry[]> entries_;
};
} // namespace alns
