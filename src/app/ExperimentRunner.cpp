#include "alns/ALNSSolver.h"
#include "alns/SearchObjectives.h"
#include "io/Parser.h"
#include "ExperimentDiagnostics.h"
#include "../test/ScheduleValidator.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>

namespace {
void readParameters(const std::string& path, alns::ALNSConfig& config) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("Cannot read parameters");
  std::set<std::string> seen;
  std::string line;
  while (std::getline(input,line)) {
    if (line.empty()) continue;
    const auto split=line.find('=');
    if (split==std::string::npos) throw std::runtime_error("Malformed parameter line");
    const std::string key=line.substr(0,split), value=line.substr(split+1);
    if (!seen.insert(key).second) throw std::runtime_error("Duplicate parameter: "+key);
    std::istringstream stream(value);
    bool matched=false;
#define PARAM(name) if (key==#name) { stream >> config.name; matched=true; }
    PARAM(maxIterations) PARAM(maxTime) PARAM(segmentIterations)
    PARAM(decayParameter) PARAM(scoreDominating) PARAM(scoreNonDominated)
    PARAM(scoreDominated) PARAM(scoreIdentical) PARAM(minRemoval) PARAM(maxRemoval)
    PARAM(useLocalSearch) PARAM(localSearchIntensity) PARAM(startTemperature)
    PARAM(coolingRate) PARAM(minTemperature) PARAM(SAScale)
    PARAM(hvImprovementThreshold) PARAM(hvStagnationLimit) PARAM(useScatterSearch)
    PARAM(useChargingRepairLS) PARAM(useSelectiveEjectionUndo) PARAM(chargingRepairTrialsPerLS)
    PARAM(chargingRepairBudget.maxEvaluations) PARAM(chargingRepairBudget.maxStations)
    PARAM(chargingRepairBudget.stationCandidates) PARAM(chargingRepairBudget.beamWidth)
    PARAM(scatterSearchConfig.refSetSizeQuality) PARAM(scatterSearchConfig.refSetSizeDiversity)
    PARAM(scatterSearchConfig.alnsItersPerCombination) PARAM(scatterSearchConfig.maxScatterIters)
    PARAM(scatterSearchConfig.timeLimitMs) PARAM(scatterSearchConfig.stagnationThreshold)
    PARAM(scatterSearchConfig.maxPRMoves)
#undef PARAM
    if (key=="reconstructionCacheMode") {
      int mode=0; stream >> mode; config.reconstructionCacheMode=static_cast<alns::ReconstructionCacheMode>(mode); matched=true;
    }
    if (key=="orOptCacheMode") {
      int mode=0; stream >> mode; config.orOptCacheMode=static_cast<alns::OrOptCacheMode>(mode); matched=true;
    }
    if (key=="chargingRepairBudget.maxDurationMs") {
      int ms=0; stream >> ms; config.chargingRepairBudget.maxDuration=std::chrono::milliseconds(ms); matched=true;
    }
    if (!matched || stream.fail() || !(stream >> std::ws).eof())
      throw std::runtime_error("Invalid parameter: "+key);
  }
  if (config.maxIterations<=0 || config.maxTime<=0 || config.segmentIterations<=0 ||
      config.minRemoval<0 || config.maxRemoval>1 || config.minRemoval>config.maxRemoval)
    throw std::runtime_error("Invalid budget/removal parameters");
}
}

int main(int argc, char** argv) {
  try {
    if (argc==2 && std::string(argv[1])=="--version") {
      std::cout << "{\"variant\":\"" << alns::search::variant << "\",\"search_objectives\":\""
                << alns::search::archiveObjectives << "\",\"interface_version\":1}\n";
      return 0;
    }
    if (argc!=5) throw std::runtime_error("Usage: evrptw input.txt parameters.cfg output seed (use run.py)");
    const std::filesystem::path output=argv[3];
    std::filesystem::create_directories(output);
    if (std::filesystem::exists(output/"results.csv")) throw std::runtime_error("Refusing to overwrite results");
    alns::ALNSConfig cfg;
    readParameters(argv[2],cfg);
    cfg.randomSeed=std::stoul(argv[4]);
    if (cfg.randomSeed==0) throw std::runtime_error("Positive explicit seed required");
    cfg.enableLogging=true; cfg.profileLocalSearch=true; cfg.logEveryIteration=true;
    auto instance=Parser::parse(argv[1]);
    std::ofstream metadata(output/"solver_metadata.json");
    metadata << "{\"variant\":\"" << alns::search::variant << "\",\"search_objectives\":\""
             << alns::search::archiveObjectives << "\",\"seed\":" << cfg.randomSeed << "}\n";
    metadata.close();
    Route::resetInsertionTimeBoundsStats();
    alns::ALNSSolver solver(instance,cfg,output.string(),"search");
    const auto front=solver.solve();
    writeExperimentDiagnostics(output,solver.getRunStatistics(),cfg);
    std::ofstream results(output/"results.csv"), schedules(output/"schedules.csv"), errors(output/"cpp_validation.txt");
    results << std::setprecision(17) << "solution,vehicles,distance,gini,max_time,valid\n";
    schedules << std::setprecision(17) << "solution,route,position,node,arrival,departure,wait,battery,load,charge\n";
    int invalid=0;
    for (size_t i=0;i<front.size();++i) {
      const auto& solution=front[i];
      bool valid=true;
      try { audit::validateSolution(solution,*instance); }
      catch(const std::exception& e) { valid=false; ++invalid; errors << i << ": " << e.what() << '\n'; }
      results << i << ',' << solution.getTotalVehicles() << ',' << solution.getTotalDistance() << ','
              << solution.getWorkloadGini() << ',' << solution.getMaxTime() << ',' << valid << '\n';
      for (size_t r=0;r<solution.getNumRoutes();++r) {
        const auto& route=solution.getRoutes()[r]; const auto& states=route.getStates();
        for(size_t p=0;p<route.size();++p) {
          const auto& s=states.at(p);
          schedules << i << ',' << r << ',' << p << ',' << route.getNodeAt(p) << ',' << s.arrivalTime << ','
                    << s.departureTime << ',' << s.timeWait << ',' << s.remainingBattery << ','
                    << s.remainingLoad << ',' << s.chargeAmount << '\n';
        }
      }
    }
    std::cout << "VALIDATION C++: solutions=" << front.size() << " invalid=" << invalid << '\n';
    return front.empty()?2:(invalid?3:0);
  } catch(const std::exception& error) { std::cerr << "ERROR: " << error.what() << '\n'; return 1; }
}
