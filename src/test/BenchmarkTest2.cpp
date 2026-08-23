#include <chrono>
#include <cfloat>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

// --- Core ---
#include "../../include/core/Instance.h"
#include "../../include/core/Solution.h"
#include "../../include/io/Parser.h"

// --- ALNS ---
#include <algorithm>

#include "../../include/alns/ALNSSolver.h"

namespace fs = std::filesystem;

// Struct để lưu kết quả benchmark
struct BenchmarkResult {
  std::string instanceName;
  int seed;
  int runNumber;
  int customers;
  int stations;
  long long timeMs;
  int paretoSize;
  int bestVehicles;
  double bestDistance;
  double bestWorkload;
  double bestMaxTime;
  bool feasible;
};

// Struct để lưu thống kê tổng hợp
struct InstanceStats {
  std::string instanceName;
  int totalRuns;
  int feasibleRuns;
  double avgTimeMs;
  int minVehicles;
  int maxVehicles;
  double avgVehicles;
  double minDistance;
  double maxDistance;
  double avgDistance;
  double minWorkload;
  double maxWorkload;
  double avgWorkload;
  double minMaxTime;
  double maxMaxTime;
  double avgMaxTime;
};

// Hàm chạy một instance với seed cụ thể và trả về kết quả
BenchmarkResult runInstance(const std::string &instancePath,
                            alns::ALNSConfig config,
                            int seed, int runNumber) {
  BenchmarkResult result;

  fs::path pathObj(instancePath);
  result.instanceName = pathObj.stem().string();
  result.seed = seed;
  result.runNumber = runNumber;

  std::cout << "\n========================================\n";
  std::cout << "  Running: " << result.instanceName
            << " (Seed=" << seed << ", Run=" << runNumber << ")\n";
  std::cout << "========================================\n";

  try {
    // Parse instance
    auto instance = Parser::parse(instancePath);
    if (!instance) {
      std::cerr << "[ERROR] Failed to parse instance!\n";
      result.feasible = false;
      return result;
    }

    result.customers = instance->getCustomers().size();
    result.stations = instance->getStations().size();

    std::cout << "Customers: " << result.customers
              << ", Stations: " << result.stations << "\n";

    // Setup output directory - phân biệt theo run number
    std::string outputDir = "logs/benchmark_test2/" + result.instanceName +
                           "/run_" + std::to_string(runNumber);
    std::string runName = result.instanceName + "_run" + std::to_string(runNumber);

    // Create solver
    auto solver = std::make_unique<alns::ALNSSolver>(instance, config,
                                                     outputDir, runName);

    // Set seed via config
    config.randomSeed = seed;

    // Run solver
    std::cout << "Starting solver...\n";
    auto startTime = std::chrono::high_resolution_clock::now();

    std::vector<Solution> paretoFront = solver->solve();

    auto endTime = std::chrono::high_resolution_clock::now();
    result.timeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        endTime - startTime)
                        .count();

    // Extract results
    result.paretoSize = paretoFront.size();

    if (!paretoFront.empty()) {
      const auto &bestSol = paretoFront[0];
      result.bestVehicles = bestSol.getTotalVehicles();
      result.bestDistance = bestSol.getTotalDistance();
      result.bestWorkload = bestSol.getWorkloadGini();
      result.bestMaxTime = bestSol.getMaxTime();
      result.feasible = bestSol.isFeasible();

      std::cout << "✓ Completed in " << result.timeMs << " ms\n";
      std::cout << "  Best: Veh=" << result.bestVehicles
                << ", Dist=" << std::fixed << std::setprecision(2)
                << result.bestDistance << "\n";
    } else {
      result.feasible = false;
      std::cout << "✗ No feasible solution found!\n";
    }

  } catch (const std::exception &e) {
    std::cerr << "[EXCEPTION] " << e.what() << "\n";
    result.feasible = false;
  }

  return result;
}

// Hàm tính thống kê tổng hợp cho một instance
InstanceStats calculateInstanceStats(const std::string &instanceName,
                                     const std::vector<BenchmarkResult> &results) {
  InstanceStats stats;
  stats.instanceName = instanceName;
  stats.totalRuns = results.size();
  stats.feasibleRuns = 0;

  double sumTime = 0;
  double sumVehicles = 0;
  double sumDistance = 0;
  double sumWorkload = 0;
  double sumMaxTime = 0;

  stats.minVehicles = INT_MAX;
  stats.maxVehicles = 0;
  stats.minDistance = DBL_MAX;
  stats.maxDistance = 0;
  stats.minWorkload = DBL_MAX;
  stats.maxWorkload = 0;
  stats.minMaxTime = DBL_MAX;
  stats.maxMaxTime = 0;

  for (const auto &r : results) {
    if (r.feasible) {
      stats.feasibleRuns++;
      sumTime += r.timeMs;
      sumVehicles += r.bestVehicles;
      sumDistance += r.bestDistance;
      sumWorkload += r.bestWorkload;
      sumMaxTime += r.bestMaxTime;

      stats.minVehicles = std::min(stats.minVehicles, r.bestVehicles);
      stats.maxVehicles = std::max(stats.maxVehicles, r.bestVehicles);
      stats.minDistance = std::min(stats.minDistance, r.bestDistance);
      stats.maxDistance = std::max(stats.maxDistance, r.bestDistance);
      stats.minWorkload = std::min(stats.minWorkload, r.bestWorkload);
      stats.maxWorkload = std::max(stats.maxWorkload, r.bestWorkload);
      stats.minMaxTime = std::min(stats.minMaxTime, r.bestMaxTime);
      stats.maxMaxTime = std::max(stats.maxMaxTime, r.bestMaxTime);
    }
  }

  if (stats.feasibleRuns > 0) {
    stats.avgTimeMs = sumTime / stats.feasibleRuns;
    stats.avgVehicles = sumVehicles / stats.feasibleRuns;
    stats.avgDistance = sumDistance / stats.feasibleRuns;
    stats.avgWorkload = sumWorkload / stats.feasibleRuns;
    stats.avgMaxTime = sumMaxTime / stats.feasibleRuns;
  } else {
    stats.avgTimeMs = 0;
    stats.avgVehicles = 0;
    stats.avgDistance = 0;
    stats.avgWorkload = 0;
    stats.avgMaxTime = 0;
  }

  return stats;
}

// Hàm ghi kết quả chi tiết ra file CSV
void writeDetailedResultsToCSV(const std::vector<BenchmarkResult> &results,
                               const std::string &filename) {
  std::ofstream file(filename);

  if (!file.is_open()) {
    std::cerr << "[ERROR] Cannot open file: " << filename << "\n";
    return;
  }

  // Header
  file << "Instance,Seed,Run,Customers,Stations,Time(ms),ParetoSize,Vehicles,"
          "Distance,Workload,MaxTime,Feasible\n";

  // Data
  for (const auto &r : results) {
    file << r.instanceName << "," << r.seed << "," << r.runNumber << ","
         << r.customers << "," << r.stations << "," << r.timeMs << ","
         << r.paretoSize << "," << r.bestVehicles << ","
         << std::fixed << std::setprecision(2) << r.bestDistance << ","
         << r.bestWorkload << "," << r.bestMaxTime << ","
         << (r.feasible ? "YES" : "NO") << "\n";
  }

  file.close();
  std::cout << "\n[INFO] Detailed results saved to: " << filename << "\n";
}

// Hàm ghi thống kê tổng hợp ra file CSV
void writeSummaryResultsToCSV(const std::vector<InstanceStats> &stats,
                               const std::string &filename) {
  std::ofstream file(filename);

  if (!file.is_open()) {
    std::cerr << "[ERROR] Cannot open file: " << filename << "\n";
    return;
  }

  // Header
  file << "Instance,TotalRuns,FeasibleRuns,AvgTime(ms),MinVehicles,MaxVehicles,"
          "AvgVehicles,MinDistance,MaxDistance,AvgDistance,MinWorkload,"
          "MaxWorkload,AvgWorkload,MinMaxTime,MaxMaxTime,AvgMaxTime\n";

  // Data
  for (const auto &s : stats) {
    file << s.instanceName << "," << s.totalRuns << "," << s.feasibleRuns << ","
         << std::fixed << std::setprecision(2) << s.avgTimeMs << ","
         << s.minVehicles << "," << s.maxVehicles << ","
         << std::fixed << std::setprecision(2) << s.avgVehicles << ","
         << std::fixed << std::setprecision(2) << s.minDistance << ","
         << std::fixed << std::setprecision(2) << s.maxDistance << ","
         << std::fixed << std::setprecision(2) << s.avgDistance << ","
         << std::fixed << std::setprecision(2) << s.minWorkload << ","
         << std::fixed << std::setprecision(2) << s.maxWorkload << ","
         << std::fixed << std::setprecision(2) << s.avgWorkload << ","
         << std::fixed << std::setprecision(2) << s.minMaxTime << ","
         << std::fixed << std::setprecision(2) << s.maxMaxTime << ","
         << std::fixed << std::setprecision(2) << s.avgMaxTime << "\n";
  }

  file.close();
  std::cout << "[INFO] Summary results saved to: " << filename << "\n";
}

int main(int argc, char *argv[]) {
  // Parse command line arguments
  std::string dataDir = "../data/test3";
  int numSeeds = 10;  // Mặc định chạy 10 lần với seed 1-10

  if (argc >= 2) {
    dataDir = argv[1];
  }
  if (argc >= 3) {
    numSeeds = std::stoi(argv[2]);
  }

  std::cout << "========================================\n";
  std::cout << "   BENCHMARK: Multi-Run Dataset        \n";
  std::cout << "========================================\n";
  std::cout << "Data Directory: " << dataDir << "\n";
  std::cout << "Number of Seeds: " << numSeeds << " (1-" << numSeeds << ")\n";
  std::cout << "========================================\n";

  // Cấu hình ALNS
  alns::ALNSConfig config;
  config.maxIterations = 22000;
  config.segmentIterations = 200;
  config.hvImprovementThreshold = 0.001;
  config.hvStagnationLimit = 5;
  config.decayParameter = 0.8;
  config.scoreDominating = 40.0;
  config.scoreNonDominated = 25.0;
  config.scoreDominated = 10.0;
  config.scoreIdentical = 0.0;
  config.minRemoval = 0.1;
  config.maxRemoval = 0.4;
  config.regretK = 3;
  config.noiseParameter = 0.1;
  config.useLocalSearch = true;
  config.useScatterSearch = true;
  config.localSearchIntensity = 30;

  // --- Scatter Search Params ---
  config.scatterSearchConfig.maxScatterIters = 5;
  config.scatterSearchConfig.alnsItersPerCombination = 10;

  config.startTemperature = 200.0;
  config.coolingRate = 0.995;
  config.minTemperature = 0.1;
  config.enableLogging = true;

  std::cout << "[INFO] ALNS Config: maxIter=" << config.maxIterations
            << ", localSearch=" << (config.useLocalSearch ? "ON" : "OFF")
            << "\n";

  // Tìm tất cả file trong thư mục
  std::vector<std::string> instanceFiles;

  try {
    for (const auto &entry : fs::directory_iterator(dataDir)) {
      if (entry.is_regular_file() && entry.path().extension() == ".txt") {
        instanceFiles.push_back(entry.path().string());
      }
    }
  } catch (const std::exception &e) {
    std::cerr << "[ERROR] Cannot read directory: " << dataDir << "\n";
    std::cerr << "        " << e.what() << "\n";
    return 1;
  }

  if (instanceFiles.empty()) {
    std::cerr << "[ERROR] No .txt files found in " << dataDir << "\n";
    return 1;
  }

  // Sắp xếp theo tên file
  std::sort(instanceFiles.begin(), instanceFiles.end());

  std::cout << "[INFO] Found " << instanceFiles.size() << " instances in "
            << dataDir << "\n\n";

  // Chạy benchmark cho tất cả instances với nhiều seed
  std::vector<BenchmarkResult> allResults;
  std::map<std::string, std::vector<BenchmarkResult>> resultsByInstance;

  auto benchmarkStart = std::chrono::high_resolution_clock::now();

  for (const auto &instancePath : instanceFiles) {
    fs::path pathObj(instancePath);
    std::string instanceName = pathObj.stem().string();

    std::cout << "\n========================================\n";
    std::cout << "  Processing Instance: " << instanceName << "\n";
    std::cout << "========================================\n";

    // Chạy với seed từ 1 đến numSeeds
    for (int seed = 1; seed <= numSeeds; seed++) {
      BenchmarkResult result = runInstance(instancePath, config, seed, seed);
      allResults.push_back(result);
      resultsByInstance[instanceName].push_back(result);
    }
  }

  auto benchmarkEnd = std::chrono::high_resolution_clock::now();
  long long totalTime = std::chrono::duration_cast<std::chrono::seconds>(
                            benchmarkEnd - benchmarkStart)
                            .count();

  // Tính thống kê tổng hợp cho từng instance
  std::vector<InstanceStats> summaryStats;
  for (const auto &[instanceName, results] : resultsByInstance) {
    InstanceStats stats = calculateInstanceStats(instanceName, results);
    summaryStats.push_back(stats);
  }

  // Tổng kết
  std::cout << "\n========================================\n";
  std::cout << "      BENCHMARK SUMMARY                 \n";
  std::cout << "========================================\n";
  std::cout << "Total Instances: " << instanceFiles.size() << "\n";
  std::cout << "Total Runs: " << allResults.size() << "\n";
  std::cout << "Total Time: " << totalTime << " seconds ("
            << (totalTime / 60.0) << " minutes)\n";

  int feasibleCount = 0;
  long long avgTime = 0;
  for (const auto &r : allResults) {
    if (r.feasible)
      feasibleCount++;
    avgTime += r.timeMs;
  }
  avgTime /= allResults.size();

  std::cout << "Feasible Solutions: " << feasibleCount << "/" << allResults.size()
            << "\n";
  std::cout << "Average Time per Run: " << avgTime << " ms\n";

  // Ghi kết quả chi tiết ra file CSV
  std::string detailedCsvFilename = "benchmark_test2_detailed.csv";
  writeDetailedResultsToCSV(allResults, detailedCsvFilename);

  // Ghi thống kê tổng hợp ra file CSV
  std::string summaryCsvFilename = "benchmark_test2_summary.csv";
  writeSummaryResultsToCSV(summaryStats, summaryCsvFilename);

  // In bảng thống kê tổng hợp
  std::cout << "\n--- Summary Statistics Table ---\n";
  std::cout << std::left << std::setw(20) << "Instance" << std::setw(8)
            << "Runs" << std::setw(8) << "Feas" << std::setw(10)
            << "AvgTime" << std::setw(8) << "MinV" << std::setw(8)
            << "MaxV" << std::setw(8) << "AvgV" << std::setw(10)
            << "MinDist" << std::setw(10) << "MaxDist" << std::setw(10)
            << "AvgDist" << "\n";
  std::cout << std::string(118, '-') << "\n";

  for (const auto &s : summaryStats) {
    std::cout << std::left << std::setw(20) << s.instanceName << std::setw(8)
              << s.totalRuns << std::setw(8) << s.feasibleRuns << std::setw(10)
              << std::fixed << std::setprecision(0) << s.avgTimeMs << std::setw(8)
              << s.minVehicles << std::setw(8) << s.maxVehicles << std::setw(8)
              << std::fixed << std::setprecision(2) << s.avgVehicles << std::setw(10)
              << std::fixed << std::setprecision(2) << s.minDistance << std::setw(10)
              << std::fixed << std::setprecision(2) << s.maxDistance << std::setw(10)
              << std::fixed << std::setprecision(2) << s.avgDistance << "\n";
  }

  std::cout << "\n========================================\n";
  std::cout << "  Benchmark completed successfully!    \n";
  std::cout << "========================================\n";
  std::cout << "Detailed results: " << detailedCsvFilename << "\n";
  std::cout << "Summary results: " << summaryCsvFilename << "\n";
  std::cout << "Log directory: logs/benchmark_test2/\n";

  return 0;
}
