#pragma once
#include "alns/ALNSSolver.h"
#include <filesystem>
#include <fstream>
inline void writeExperimentDiagnostics(const std::filesystem::path& output,
    const alns::ALNSRunStatistics& stats, const alns::ALNSConfig& cfg) {
  const int cacheMode=static_cast<int>(cfg.reconstructionCacheMode);
  const int orOptCacheMode=static_cast<int>(cfg.orOptCacheMode);
  const bool selectiveEjection=cfg.useSelectiveEjectionUndo;
    const auto insertionBounds=Route::getInsertionTimeBoundsStats();
    std::ofstream boundsReport(output/"insertion_time_bounds.json");
    boundsReport<<"{\"enabled\":"<<(Route::insertionTimeBoundsEnabled()?"true":"false")
      <<",\"requests\":"<<insertionBounds.requests<<",\"builds\":"<<insertionBounds.builds
      <<",\"rejections\":"<<insertionBounds.rejections<<"}\n";
    const auto& undo=stats.ejectionUndo;
    const auto& block=stats.orOptCache;
    std::ofstream blockReport(output/"or_opt_cache.json");
    blockReport<<"{\"mode\":"<<orOptCacheMode<<",\"capacity\":"<<block.capacity<<",\"bytes\":"<<block.bytes
      <<",\"sessions\":"<<block.sessions<<",\"lookups\":"<<block.lookups
      <<",\"repeated_blocks\":"<<block.repeatedBlocks<<",\"skips\":"<<block.skips
      <<",\"inserts\":"<<block.inserts<<",\"evictions\":"<<block.evictions
      <<",\"peak_entries\":"<<block.peakEntries<<",\"exhausted_blocks\":"<<block.exhaustedBlocks
      <<",\"successful_blocks\":"<<block.successfulBlocks<<",\"candidate_positions\":"<<block.candidatePositions
      <<",\"repeated_candidate_positions\":"<<block.repeatedCandidatePositions
      <<",\"evaluations\":"<<block.evaluations<<",\"repeated_evaluations\":"<<block.repeatedEvaluations
      <<",\"mismatches\":"<<block.mismatches<<",\"generation_changes\":"<<block.generationChanges
      <<",\"topology_rebuilds\":"<<block.topologyRebuilds<<"}\n";
    std::ofstream undoReport(output/"ejection_undo.json");
    undoReport<<"{\"selective\":"<<selectiveEjection<<",\"checkpoints\":"<<undo.checkpoints
      <<",\"full_snapshots\":"<<undo.fullSnapshots<<",\"saved_routes\":"<<undo.savedRoutes
      <<",\"restored_routes\":"<<undo.restoredRoutes<<",\"rollbacks\":"<<undo.rollbacks
      <<",\"commits\":"<<undo.commits<<",\"peak_undo_routes\":"<<undo.peakUndoRoutes
      <<",\"place_calls\":"<<undo.placeCalls<<",\"branch_trials\":"<<undo.branchTrials<<"}\n";
    const auto& cache=stats.reconstructionCache;
    std::ofstream cacheReport(output/"reconstruction_cache.json");
    cacheReport<<"{\"mode\":"<<cacheMode<<",\"capacity\":"<<alns::ReconstructionCache::MaxEntries
      <<",\"max_key_nodes\":"<<alns::ReconstructionCache::MaxKeyNodes
      <<",\"requests\":"<<cache.requests<<",\"computations\":"<<cache.computations
      <<",\"successes\":"<<cache.successes<<",\"failures\":"<<cache.failures
      <<",\"repeated_failures\":"<<cache.repeatedFailures<<",\"hits\":"<<cache.hits
      <<",\"inserts\":"<<cache.inserts<<",\"sessions\":"<<cache.sessions
      <<",\"capacity_clears\":"<<cache.capacityClears<<",\"peak_entries\":"<<cache.peakEntries
      <<",\"oversized\":"<<cache.oversized<<"}\n";
    std::ofstream profile(output/"local_search_profile.json");
    profile<<"{\"enabled\":"<<(stats.localSearchProfile.enabled?"true":"false")
           <<",\"clock\":\"steady_wall_nanoseconds\",\"metrics\":{";
    for(size_t i=0;i<alns::lsMetricNames.size();++i) {
      const auto& entry=stats.localSearchProfile.entries[i];
      if(i) profile<<',';
      profile<<'\"'<<alns::lsMetricNames[i]<<"\":{\"calls\":"<<entry.calls
             <<",\"vnd_improvements\":"<<entry.improvements
             <<",\"inclusive_ns\":"<<entry.inclusiveNs<<",\"self_ns\":"<<entry.selfNs<<'}';
    }
    profile<<"}}\n";
    std::ofstream timing(output/"run_statistics.json");
    timing<<"{\"iterations\":"<<stats.completedIterations
          <<",\"stop_reason\":\""<<stats.stopReason<<"\",\"initialization_ms\":"<<stats.initializationMs
          <<",\"search_ms\":"<<stats.searchMs<<",\"total_ms\":"<<stats.totalMs
          <<",\"charging_repair\":{\"calls\":"<<stats.chargingRepair.calls
          <<",\"feasible\":"<<stats.chargingRepair.feasible
          <<",\"exhausted\":"<<stats.chargingRepair.exhausted
          <<",\"evaluations\":"<<stats.chargingRepair.evaluations
          <<",\"elapsed_us\":"<<stats.chargingRepair.elapsedUs
          <<",\"relocate_accepted\":"<<stats.chargingRepair.relocateAccepted
          <<",\"inter_two_opt_accepted\":"<<stats.chargingRepair.interTwoOptAccepted
          <<",\"vehicles_reduced\":"<<stats.chargingRepair.vehiclesReduced<<"}}\n";
}
