// src/logger/ComprehensiveLogger.cpp
#include "../../include/logger/ComprehensiveLogger.h"
#include "../../include/alns/ALNSSolver.h"
#include "../../include/alns/ParetoArchive.h"
#include "../../include/core/Solution.h"
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>


namespace logging {
namespace fs = std::filesystem;

// --- 1. Constructor & Destructor ---

ComprehensiveLogger::ComprehensiveLogger(const std::string &outputDirectory,
                                         const std::string &runName) {
  try {
    fs::create_directories(outputDirectory);
  } catch (const std::exception &e) {
    std::cerr << "FATAL ERROR: Logger could not create directory: "
              << outputDirectory << ". Error: " << e.what() << std::endl;
    return;
  }

  this->filePrefix = outputDirectory + "/" + runName;

  // Mở 7 file
  configFile.open(filePrefix + "_config.txt");
  progressFile.open(filePrefix + "_progress.csv");
  operatorFile.open(filePrefix + "_operators.csv");
  evolutionLogFile.open(filePrefix + "_evolution_log.txt");
  frontObjFile.open(filePrefix + "_front_objectives.csv");
  frontDetailFile.open(filePrefix + "_front_details.txt");
  summaryFile.open(filePrefix + "_summary.txt");

  if (!configFile.is_open() || !progressFile.is_open() ||
      !operatorFile.is_open() || !evolutionLogFile.is_open() ||
      !frontObjFile.is_open() || !frontDetailFile.is_open() ||
      !summaryFile.is_open()) {
    std::cerr << "FATAL ERROR: Logger could not open all output files."
              << std::endl;
  }

  // Ghi headers
  initConfigFile();
  initProgressFile();
  initOperatorFile();
  initEvolutionLogFile();
  initFrontObjectiveFile();
  initFrontDetailFile();
  initSummaryFile();

  auto set_format = [](std::ofstream &file) {
    file << std::fixed << std::setprecision(4);
  };
  set_format(progressFile);
  set_format(operatorFile);
  set_format(frontObjFile);
  set_format(summaryFile);
}

ComprehensiveLogger::~ComprehensiveLogger() {
  if (configFile.is_open())
    configFile.close();
  if (progressFile.is_open())
    progressFile.close();
  if (operatorFile.is_open())
    operatorFile.close();
  if (evolutionLogFile.is_open())
    evolutionLogFile.close();
  if (frontObjFile.is_open())
    frontObjFile.close();
  if (frontDetailFile.is_open())
    frontDetailFile.close();
  if (summaryFile.is_open())
    summaryFile.close();
}

// --- 2. Các hàm ghi Header (Init) ---

void ComprehensiveLogger::initConfigFile() {
  std::time_t t = std::time(nullptr);
  char time_buf[100];
  std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S",
                std::localtime(&t));
  configFile << "--- ALNS Run Configuration ---\n";
  configFile << "RunPrefix: " << filePrefix << "\n";
  configFile << "Timestamp: " << time_buf << "\n";
  configFile << "--------------------------------\n";
}

void ComprehensiveLogger::initProgressFile() {
  progressFile << "Iteration,Timestamp_ms,ArchiveSize,"
               << "BestDistance,BestVehicles,BestMaxTime,BestWorkloadGini\n";
}

void ComprehensiveLogger::initOperatorFile() {
  operatorFile << "Segment,OperatorName,Type,Weight,Score,UsageCount\n";
}

void ComprehensiveLogger::initEvolutionLogFile() {
  evolutionLogFile << "--- ALNS Solution Evolution Log ---\n";
  evolutionLogFile
      << "(Logs all accepted non-dominated/dominating solutions)\n\n";
}

void ComprehensiveLogger::initFrontObjectiveFile() {
  frontObjFile << "SolutionID,TotalVehicles,TotalDistance,WorkloadGini,MaxTime,"
                  "IsFeasible\n";
}

void ComprehensiveLogger::initFrontDetailFile() {
  frontDetailFile << "--- FINAL PARETO FRONT DETAILS ---\n\n";
}

void ComprehensiveLogger::initSummaryFile() {
  summaryFile << "--- ALNS Run Summary ---\n";
}

// --- 3. Các hàm Log chính ---

void ComprehensiveLogger::logConfig(const alns::ALNSConfig &config) {
  if (!configFile.is_open())
    return;
  configFile << "maxIterations: " << config.maxIterations << "\n"
             << "segmentIterations: " << config.segmentIterations << "\n"
             << "decayParameter: " << config.decayParameter << "\n"
             << "scoreDominating: " << config.scoreDominating
             << "\n"
             // ... (Ghi TẤT CẢ các tham số khác trong config) ...
             << "enableLogging: " << (config.enableLogging ? "true" : "false")
             << "\n";
}

void ComprehensiveLogger::logProgress(int iteration, long long time_ms,
                                      const ParetoArchive &archive) {
  if (!progressFile.is_open())
    return;
  double bestDist = std::numeric_limits<double>::infinity();
  int bestVeh = std::numeric_limits<int>::max();
  double bestMaxTime = std::numeric_limits<double>::infinity();
  double bestWorkloadVar = std::numeric_limits<double>::infinity();
  if (archive.getSize() > 0) {
    for (const auto &sol : archive.getFront()) {
      if (sol.getTotalDistance() < bestDist)
        bestDist = sol.getTotalDistance();
      if (sol.getTotalVehicles() < bestVeh)
        bestVeh = sol.getTotalVehicles();
      if (sol.getMaxTime() < bestMaxTime)
        bestMaxTime = sol.getMaxTime();
      if (sol.getWorkloadGini() < bestWorkloadVar)
        bestWorkloadVar = sol.getWorkloadGini();
    }
  }
  progressFile << iteration << "," << time_ms << "," << archive.getSize() << ","
               << bestDist << "," << bestVeh << "," << bestMaxTime << ","
               << bestWorkloadVar << "\n";
}

void ComprehensiveLogger::logOperatorSegment(
    int segment, const alns::OperatorPool &destroyPool,
    const alns::OperatorPool &repairPool) {
  if (!operatorFile.is_open())
    return;
  for (size_t i = 0; i < destroyPool.operators.size(); ++i) {
    operatorFile << segment << "," << destroyPool.operators[i]->getName() << ","
                 << "Destroy," << destroyPool.weights[i] << ","
                 << destroyPool.scores[i] << "," << destroyPool.usages[i]
                 << "\n";
  }
  for (size_t i = 0; i < repairPool.operators.size(); ++i) {
    operatorFile << segment << "," << repairPool.operators[i]->getName() << ","
                 << "Repair," << repairPool.weights[i] << ","
                 << repairPool.scores[i] << "," << repairPool.usages[i] << "\n";
  }
}

void ComprehensiveLogger::logEvolutionStep(int iteration,
                                           const std::string &destroyOp,
                                           const std::string &repairOp,
                                           const std::string &result,
                                           const Solution &newSolution) {
  if (!evolutionLogFile.is_open())
    return;
  evolutionLogFile << "----------------------------------------\n"
                   << "[Iteration: " << iteration << " | Result: " << result
                   << "]\n"
                   << "[Operators: " << destroyOp << " / " << repairOp << "]\n"
                   << "----------------------------------------\n";
  evolutionLogFile << newSolution.toString() << "\n\n";
  evolutionLogFile.flush();
}

void ComprehensiveLogger::logFinalFront(const ParetoArchive &archive) {
  if (!frontObjFile.is_open() || !frontDetailFile.is_open())
    return;
  int solutionID = 1;
  for (const auto &sol : archive.getFront()) {
    frontObjFile << solutionID << "," << sol.getTotalVehicles() << ","
                 << sol.getTotalDistance() << "," << sol.getWorkloadGini()
                 << "," << sol.getMaxTime() << ","
                 << (sol.isFeasible() ? "true" : "false") << "\n";
    frontDetailFile << "========================================\n"
                    << "== Solution ID: " << solutionID << "\n"
                    << "========================================\n"
                    << sol.toString() << "\n\n";
    solutionID++;
  }
}

void ComprehensiveLogger::logSummary(long long total_ms, int total_iter,
                                     int final_archive_size) {
  if (!summaryFile.is_open())
    return;
  summaryFile << "TotalRunTime_ms: " << total_ms << "\n"
              << "TotalRunTime_s: " << (double)total_ms / 1000.0 << "\n"
              << "TotalIterations: " << total_iter << "\n"
              << "FinalArchiveSize: " << final_archive_size << "\n";
}

} // namespace logging