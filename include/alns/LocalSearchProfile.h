#pragma once
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace alns {
// Fixed storage: no allocations, clocks or logging when disabled. Timings are
// monotonic wall time, not CPU time. Self time subtracts instrumented children.
enum class LSMetric : std::size_t {
  Run, Distance, Charging, VehicleReduction, IntensifiedReduction,
  Relocate, Swap, OrOpt, TwoOpt, InterTwoOpt, IntraOrOpt, CrossExchange,
  OrOptReversed, EmptySmallest, Ejection, EliminateSmallest, SmartMerge,
  TriMerge, SegmentExchange, ContextRebuild, ContextPatch, RemovalRanking,
  EvaluateMove, ApplyMove, ChargingRepair, Reconstruction, EjectionSnapshot, EjectionRestore, OrOptCache, Count
};
inline constexpr std::array<const char*, static_cast<std::size_t>(LSMetric::Count)> lsMetricNames{
  "run", "distance", "charging", "vehicle_reduction", "intensified_reduction",
  "relocate", "swap", "or_opt", "two_opt", "inter_two_opt", "intra_or_opt",
  "cross_exchange", "or_opt_reversed", "empty_smallest", "ejection",
  "eliminate_smallest", "smart_merge", "tri_merge", "segment_exchange",
  "context_rebuild", "context_patch", "removal_ranking", "evaluate_move",
  "apply_move", "charging_repair", "reconstruction", "ejection_snapshot", "ejection_restore", "or_opt_cache"
};
struct LSProfileEntry {
  std::uint64_t calls = 0, improvements = 0;
  std::int64_t inclusiveNs = 0, selfNs = 0;
};
struct LocalSearchProfile {
  bool enabled = false;
  std::array<LSProfileEntry, static_cast<std::size_t>(LSMetric::Count)> entries{};
};
class LocalSearchProfiler {
public:
  class Scope {
  public:
    Scope(LocalSearchProfiler& owner, LSMetric metric) : owner_(owner) {
      if (!owner_.data.enabled) return;
      entry_ = &owner_.data.entries[static_cast<std::size_t>(metric)];
      parent_ = owner_.active_;
      owner_.active_ = this;
      ++entry_->calls;
      start_ = Clock::now();
    }
    ~Scope() {
      if (!entry_) return;
      const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start_).count();
      entry_->inclusiveNs += ns;
      entry_->selfNs += ns-childrenNs_;
      owner_.active_ = parent_;
      if (parent_) parent_->childrenNs_ += ns;
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
  private:
    using Clock = std::chrono::steady_clock;
    LocalSearchProfiler& owner_;
    LSProfileEntry* entry_ = nullptr;
    Scope* parent_ = nullptr;
    Clock::time_point start_;
    std::int64_t childrenNs_ = 0;
  };
  LocalSearchProfile data;
private:
  Scope* active_ = nullptr;
};
} // namespace alns
