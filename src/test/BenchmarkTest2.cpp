#include <chrono>
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
#include "../../include/alns/ALNSSolver.h"

namespace fs = std::filesystem;

// Struct để lưu kết quả benchmark
struct BenchmarkResult {
  std::string instanceName;
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

// Hàm chạy một instance và trả về kết quả
BenchmarkResult runInstance(const std::string &instancePath,
                            const alns::ALNSConfig &config) {
  BenchmarkResult result;

  fs::path pathObj(instancePath);
  result.instanceName = pathObj.stem().string();

  std::cout << "\n========================================\n";
  std::cout << "  Running: " << result.instanceName << "\n";
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

    // Setup output directory
    std::string outputDir = "logs/benchmark_test2/" + result.instanceName;
    std::string runName = result.instanceName;

    // Create solver
    auto solver = std::make_unique<alns::ALNSSolver>(instance, config,
                                                     outputDir, runName);

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
      result.bestWorkload = bestSol.getWorkloadVariance();
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

// Hàm ghi kết quả ra file CSV
void writeResultsToCSV(const std::vector<BenchmarkResult> &results,
                       const std::string &filename) {
  std::ofstream file(filename);

  if (!file.is_open()) {
    std::cerr << "[ERROR] Cannot open file: " << filename << "\n";
    return;
  }

  // Header
  file << "Instance,Customers,Stations,Time(ms),ParetoSize,Vehicles,Distance,"
          "Workload,MaxTime,Feasible\n";

  // Data
  for (const auto &r : results) {
    file << r.instanceName << "," << r.customers << "," << r.stations << ","
         << r.timeMs << "," << r.paretoSize << "," << r.bestVehicles << ","
         << std::fixed << std::setprecision(2) << r.bestDistance << ","
         << r.bestWorkload << "," << r.bestMaxTime << ","
         << (r.feasible ? "YES" : "NO") << "\n";
  }

  file.close();
  std::cout << "\n[INFO] Results saved to: " << filename << "\n";
}

int main(int argc, char *argv[]) {
  std::cout << "========================================\n";
  std::cout << "   BENCHMARK: ../data/test2 Dataset    \n";
  std::cout << "========================================\n";

  // Cấu hình ALNS
  alns::ALNSConfig config;
  config.maxIterations = 2000;
  config.segmentIterations = 50;
  config.hvImprovementThreshold = 0.001;
  config.hvStagnationLimit = 5;
  config.decayParameter = 0.8;
  config.scoreDominating = 25.0;
  config.scoreNonDominated = 10.0;
  config.scoreDominated = 5.0;
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

  config.startTemperature = 100.0;
  config.coolingRate = 0.995;
  config.minTemperature = 0.1;
  config.enableLogging = true;

  std::cout << "[INFO] ALNS Config: maxIter=" << config.maxIterations
            << ", localSearch=" << (config.useLocalSearch ? "ON" : "OFF")
            << "\n";

  // Tìm tất cả file trong ../data/test2
  std::string dataDir = "../data/test2";
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

  // Chạy benchmark cho tất cả instances
  std::vector<BenchmarkResult> results;

  auto benchmarkStart = std::chrono::high_resolution_clock::now();

  for (const auto &instancePath : instanceFiles) {
    BenchmarkResult result = runInstance(instancePath, config);
    results.push_back(result);
  }

  auto benchmarkEnd = std::chrono::high_resolution_clock::now();
  long long totalTime = std::chrono::duration_cast<std::chrono::seconds>(
                            benchmarkEnd - benchmarkStart)
                            .count();

  // Tổng kết
  std::cout << "\n========================================\n";
  std::cout << "      BENCHMARK SUMMARY                 \n";
  std::cout << "========================================\n";
  std::cout << "Total Instances: " << results.size() << "\n";
  std::cout << "Total Time: " << totalTime << " seconds\n";

  int feasibleCount = 0;
  long long avgTime = 0;
  for (const auto &r : results) {
    if (r.feasible)
      feasibleCount++;
    avgTime += r.timeMs;
  }
  avgTime /= results.size();

  std::cout << "Feasible Solutions: " << feasibleCount << "/" << results.size()
            << "\n";
  std::cout << "Average Time per Instance: " << avgTime << " ms\n";

  // Ghi kết quả ra file CSV
  std::string csvFilename = "benchmark_test2_results.csv";
  writeResultsToCSV(results, csvFilename);

  // In bảng kết quả
  std::cout << "\n--- Results Table ---\n";
  std::cout << std::left << std::setw(20) << "Instance" << std::setw(8)
            << "Cust" << std::setw(10) << "Time(ms)" << std::setw(6) << "Veh"
            << std::setw(12) << "Distance" << std::setw(10) << "Feasible"
            << "\n";
  std::cout << std::string(76, '-') << "\n";

  for (const auto &r : results) {
    std::cout << std::left << std::setw(20) << r.instanceName << std::setw(8)
              << r.customers << std::setw(10) << r.timeMs << std::setw(6)
              << r.bestVehicles << std::setw(12) << std::fixed
              << std::setprecision(2) << r.bestDistance << std::setw(10)
              << (r.feasible ? "YES" : "NO") << "\n";
  }

  std::cout << "\n========================================\n";
  std::cout << "  Benchmark completed successfully!    \n";
  std::cout << "========================================\n";

  return 0;
}
