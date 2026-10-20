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
    if(argc<5) throw std::runtime_error("Usage: ALNSAuditRunner input output seconds seed [maxIterations=25000]");
    const std::filesystem::path output=argv[2];
    std::filesystem::create_directories(output);
    auto inst=Parser::parse(argv[1]);
    alns::ALNSConfig cfg;
    cfg.maxIterations=argc>5?std::stoi(argv[5]):25000; cfg.segmentIterations=200;
    cfg.maxTime=std::stoll(argv[3])*1000; cfg.randomSeed=std::stoul(argv[4]);
    cfg.decayParameter=.85; cfg.scoreDominating=40; cfg.scoreNonDominated=25;
    cfg.scoreDominated=10; cfg.scoreIdentical=15; cfg.minRemoval=.15; cfg.maxRemoval=.55;
    cfg.localSearchIntensity=25; cfg.startTemperature=200; cfg.coolingRate=.9993;
    cfg.minTemperature=.05; cfg.SAScale=50; cfg.hvStagnationLimit=20;
    cfg.useScatterSearch=false; cfg.enableLogging=true;
    audit::require(cfg.maxTime>0 && cfg.maxIterations>0,"positive budget required");
    alns::ALNSSolver solver(inst,cfg,output.string(),"audit");
    const auto start=std::chrono::steady_clock::now();
    auto front=solver.solve();
    const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
    const auto& stats=solver.getRunStatistics();
    std::ofstream timing(output/"run_statistics.json");
    timing<<"{\"iterations\":"<<stats.completedIterations
          <<",\"stop_reason\":\""<<stats.stopReason<<"\",\"initialization_ms\":"<<stats.initializationMs
          <<",\"search_ms\":"<<stats.searchMs<<",\"total_ms\":"<<stats.totalMs<<"}\n";
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
