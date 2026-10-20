#include "ScheduleValidator.h"
#include "alns/ALNSSolver.h"
#include "io/Parser.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>

int main(int argc,char** argv) {
  try {
    if(argc<5) throw std::runtime_error("Usage: ALNSAuditRunner input output seconds seed [maxIterations=25000] [chargingRepair=0|1] [profileLS=0|1] [reconstructionCache=0|1|2]");
    const std::filesystem::path output=argv[2];
    if(std::filesystem::exists(output)) throw std::runtime_error("Refusing to overwrite output directory");
    std::filesystem::create_directories(output);
    auto inst=Parser::parse(argv[1]);
    alns::ALNSConfig cfg;
    cfg.maxIterations=argc>5?std::stoi(argv[5]):25000; cfg.segmentIterations=200;
    cfg.maxTime=std::stoll(argv[3])*1000; cfg.randomSeed=std::stoul(argv[4]);
    const int chargingRepair=argc>6?std::stoi(argv[6]):0;
    audit::require(chargingRepair==0 || chargingRepair==1,"chargingRepair must be 0 or 1");
    cfg.useChargingRepairLS=chargingRepair==1;
    const int profileLS=argc>7?std::stoi(argv[7]):0;
    audit::require(profileLS==0 || profileLS==1,"profileLS must be 0 or 1");
    cfg.profileLocalSearch=profileLS==1;
    const int cacheMode=argc>8?std::stoi(argv[8]):static_cast<int>(cfg.reconstructionCacheMode);
    audit::require(cacheMode>=0 && cacheMode<=2,"reconstructionCache must be 0 (off), 1 (observe), or 2 (enabled)");
    cfg.reconstructionCacheMode=static_cast<alns::ReconstructionCacheMode>(cacheMode);
    cfg.decayParameter=.85; cfg.scoreDominating=40; cfg.scoreNonDominated=25;
    cfg.scoreDominated=10; cfg.scoreIdentical=15; cfg.minRemoval=.15; cfg.maxRemoval=.55;
    cfg.localSearchIntensity=25; cfg.startTemperature=200; cfg.coolingRate=.9993;
    cfg.minTemperature=.05; cfg.SAScale=50; cfg.hvStagnationLimit=20;
    cfg.useScatterSearch=false; cfg.enableLogging=true;
    audit::require(cfg.maxTime>0 && cfg.maxIterations>0,"positive budget required");
    std::ofstream configuration(output/"audit_parameters.json");
    configuration<<"{\"seed\":"<<cfg.randomSeed<<",\"max_time_ms\":"<<cfg.maxTime
                 <<",\"max_iterations\":"<<cfg.maxIterations<<",\"hv_stagnation_limit\":"<<cfg.hvStagnationLimit
                 <<",\"charging_repair\":"<<cfg.useChargingRepairLS
                 <<",\"profile_local_search\":"<<cfg.profileLocalSearch
                 <<",\"reconstruction_cache_mode\":"<<cacheMode
                 <<",\"repair_max_evaluations\":"<<cfg.chargingRepairBudget.maxEvaluations
                 <<",\"repair_max_stations\":"<<cfg.chargingRepairBudget.maxStations
                 <<",\"repair_station_candidates\":"<<cfg.chargingRepairBudget.stationCandidates
                 <<",\"repair_beam_width\":"<<cfg.chargingRepairBudget.beamWidth
                 <<",\"repair_max_duration_ms\":"<<cfg.chargingRepairBudget.maxDuration.count()
                 <<",\"repair_trials_per_ls\":"<<cfg.chargingRepairTrialsPerLS<<"}\n";
    configuration.close();
    alns::ALNSSolver solver(inst,cfg,output.string(),"audit");
    const auto start=std::chrono::steady_clock::now();
    auto front=solver.solve();
    const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
    const auto& stats=solver.getRunStatistics();
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
    std::ofstream results(output/"results.csv"),schedules(output/"schedules.csv"),errors(output/"validation.txt");
    results<<std::setprecision(15)<<"solution,vehicles,distance,gini,max_time,elapsed_ms,valid\n";
    schedules<<std::setprecision(15)<<"solution,route,position,node,arrival,departure,wait,battery,load,charge\n";
    int invalid=0;
    for(size_t i=0;i<front.size();++i) {
      auto& sol=front[i];
      // Check reported metrics before any schedule-export getter is called.
      bool valid=true;
      try { audit::validateSolution(sol,*inst); }
      catch(const std::exception& e) { valid=false; ++invalid; errors<<i<<": "<<e.what()<<'\n'; }
      results<<i<<','<<sol.getTotalVehicles()<<','<<sol.getTotalDistance()<<','<<sol.getWorkloadGini()<<','
             <<sol.getMaxTime()<<','<<elapsed<<','<<valid<<'\n';
      const Solution& readOnly=sol;
      for(size_t r=0;r<readOnly.getNumRoutes();++r) {
        const auto& route=readOnly.getRoutes()[r];
        const auto& states=route.getStates();
        for(size_t p=0;p<route.size();++p) {
          const auto& s=states.at(p);
          schedules<<i<<','<<r<<','<<p<<','<<route.getNodeAt(p)<<','<<s.arrivalTime<<','<<s.departureTime<<','
                   <<s.timeWait<<','<<s.remainingBattery<<','<<s.remainingLoad<<','<<s.chargeAmount<<'\n';
        }
      }
    }
    std::cout<<"AUDIT front="<<front.size()<<" invalid="<<invalid<<" elapsed_ms="<<elapsed<<'\n';
    return front.empty()?2:(invalid?3:0);
  } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
