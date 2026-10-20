#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <unordered_set>
#include <vector>

namespace alns {
enum class ReconstructionCacheMode { Off = 0, Observe = 1, Enabled = 2 };
struct ReconstructionCacheStatistics {
  std::uint64_t requests = 0, computations = 0, successes = 0, failures = 0;
  std::uint64_t repeatedFailures = 0, hits = 0, inserts = 0;
  std::uint64_t sessions = 0, capacityClears = 0, peakEntries = 0, oversized = 0;
};

// Failure memoization of the deterministic reconstruction helper, not a proof
// of infeasibility. Scope is one LS entry point; instance data must stay fixed
// during it. No route ID/index or objective is used to identify the input.
class ReconstructionCache {
public:
  struct Key {
    std::vector<int> nodes, incoming;
    std::array<double, 3> vehicle; // capacity, battery, energy rate
    int maxStations;
    bool operator==(const Key& b) const {
      return maxStations == b.maxStations && vehicle == b.vehicle &&
             nodes == b.nodes && incoming == b.incoming;
    }
  };
  struct Hash {
    std::size_t operator()(const Key& k) const {
      std::size_t h = std::hash<int>{}(k.maxStations);
      auto mix = [&](std::size_t v) { h ^= v + 0x9e3779b9U + (h << 6) + (h >> 2); };
      for (double v : k.vehicle) mix(std::hash<double>{}(v));
      mix(k.nodes.size());
      for (int n : k.nodes) mix(std::hash<int>{}(n));
      mix(k.incoming.size());
      for (int n : k.incoming) mix(std::hash<int>{}(n));
      return h;
    }
  };
  static constexpr std::size_t MaxKeyNodes = 512;
  static constexpr std::size_t MaxEntries = 4096;

  // Configure/reset only between entry points. Observe follows the same bounded
  // table policy but never avoids a computation, allowing direct comparison.
  void configure(ReconstructionCacheMode mode, std::size_t capacity = MaxEntries) {
    mode_ = mode; capacity_ = std::min(capacity, MaxEntries); reset();
  }
  void reset() { failures_.clear(); stats = {}; }
  bool active() const { return active_ && mode_ != ReconstructionCacheMode::Off && capacity_ > 0; }
  bool skip(const Key& key) {
    if (failures_.find(key) == failures_.end()) return false;
    ++stats.repeatedFailures;
    if (mode_ != ReconstructionCacheMode::Enabled) return false;
    ++stats.hits;
    return true;
  }
  void failed(Key key) {
    if (failures_.find(key) != failures_.end()) return;
    if (failures_.size() >= capacity_) { failures_.clear(); ++stats.capacityClears; }
    failures_.insert(std::move(key));
    ++stats.inserts;
    stats.peakEntries = std::max<std::uint64_t>(stats.peakEntries, failures_.size());
  }
  class Session {
  public:
    explicit Session(ReconstructionCache& cache) : cache_(cache), owns_(!cache.active_) {
      if (!owns_) return;
      cache_.failures_.clear(); cache_.active_ = true;
      if (cache_.active()) ++cache_.stats.sessions;
    }
    ~Session() { if (owns_) { cache_.failures_.clear(); cache_.active_ = false; } }
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
  private:
    ReconstructionCache& cache_;
    bool owns_;
  };
  ReconstructionCacheStatistics stats;
private:
  ReconstructionCacheMode mode_ = ReconstructionCacheMode::Off;
  std::size_t capacity_ = MaxEntries;
  bool active_ = false;
  std::unordered_set<Key, Hash> failures_;
};
} // namespace alns
