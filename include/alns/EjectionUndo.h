#pragma once
#include "../core/Route.h"
#include "LocalSearchProfile.h"
#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace alns {
struct EjectionUndoStatistics {
  std::uint64_t checkpoints=0, fullSnapshots=0, savedRoutes=0, restoredRoutes=0;
  std::uint64_t rollbacks=0, commits=0, peakUndoRoutes=0;
  std::uint64_t placeCalls=0, branchTrials=0;
};

// Stage B never inserts/erases routes. Each nested checkpoint remembers the
// first state of a route it modifies. Committed children remain undoable by an
// outer checkpoint; failed children restore in reverse order. Full snapshots
// remain available as a reference mode with identical search decisions.
class EjectionUndo {
  struct Entry { int route; Route before; };
public:
  class Checkpoint {
  public:
    explicit Checkpoint(EjectionUndo& owner) : owner_(owner), parent_(owner.active_), mark_(owner.entries_.size()) {
      ++owner_.stats_.checkpoints;
      if (!owner_.selective_) {
        LocalSearchProfiler::Scope timer(owner_.profiler_,LSMetric::EjectionSnapshot);
        backup_=owner_.routes_;
        ++owner_.stats_.fullSnapshots;
        owner_.stats_.savedRoutes+=backup_.size();
      }
      owner_.active_=this;
    }
    ~Checkpoint() {
      if (!committed_) {
        LocalSearchProfiler::Scope timer(owner_.profiler_,LSMetric::EjectionRestore);
        ++owner_.stats_.rollbacks;
        if (owner_.selective_) {
          while (owner_.entries_.size()>mark_) {
            auto& entry=owner_.entries_.back();
            owner_.routes_[entry.route]=std::move(entry.before);
            owner_.entries_.pop_back(); ++owner_.stats_.restoredRoutes;
          }
        } else {
          owner_.stats_.restoredRoutes+=backup_.size();
          owner_.routes_=std::move(backup_);
        }
      }
      owner_.active_=parent_;
    }
    void commit() { committed_=true; ++owner_.stats_.commits; }
    Checkpoint(const Checkpoint&)=delete;
    Checkpoint& operator=(const Checkpoint&)=delete;
  private:
    friend class EjectionUndo;
    EjectionUndo& owner_;
    Checkpoint* parent_;
    std::size_t mark_;
    bool committed_=false;
    std::vector<Route> backup_;
  };
  EjectionUndo(std::vector<Route>& routes,bool selective,EjectionUndoStatistics& stats,LocalSearchProfiler& profiler)
    : routes_(routes),selective_(selective),stats_(stats),profiler_(profiler) {
    if (selective_) entries_.reserve(8); // depth <= 5 plus a direct insertion
  }
  void touch(int route) {
    if (!selective_) return;
    // All mutations must be inside a checkpoint; no route identity is needed
    // because route indices stay fixed throughout Stage B.
    for (std::size_t i=active_->mark_;i<entries_.size();++i)
      if (entries_[i].route==route) return;
    LocalSearchProfiler::Scope timer(profiler_,LSMetric::EjectionSnapshot);
    entries_.push_back({route,routes_[route]});
    ++stats_.savedRoutes;
    stats_.peakUndoRoutes=std::max<std::uint64_t>(stats_.peakUndoRoutes,entries_.size());
  }
  void clearHistory() { entries_.clear(); }
private:
  std::vector<Route>& routes_;
  bool selective_;
  EjectionUndoStatistics& stats_;
  LocalSearchProfiler& profiler_;
  std::vector<Entry> entries_;
  Checkpoint* active_=nullptr;
};
} // namespace alns
