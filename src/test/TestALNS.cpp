#include <chrono>
#include <filesystem> // [NEW] Thêm thư viện này để xử lý đường dẫn file
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

namespace fs = std::filesystem; // [NEW] Alias cho ngắn gọn

// Hàm in kết quả (Helper)
void printSolutionSummary(const Solution &sol) {
  std::cout << "\n----------------------------------------\n";
  std::cout << "           SOLUTION SUMMARY             \n";
  std::cout << "----------------------------------------\n";
  std::cout << std::fixed << std::setprecision(2);
  std::cout << "Total Vehicles:   " << sol.getTotalVehicles() << "\n";
  std::cout << "Total Distance:   " << sol.getTotalDistance() << "\n";
  // std::cout << "Workload Variance:" << sol.getWorkloadGini() << "\n";
  std::cout << "Workload Variance:" << sol.getWorkloadGini() << "\n";
  std::cout << "Max Time:         " << sol.getMaxTime() << "\n";
  std::cout << "Feasible:         " << (sol.isFeasible() ? "YES" : "NO")
            << "\n";
  std::cout << "----------------------------------------\n";
}

int main(int argc, char *argv[]) {
  std::cout << "========================================\n";
  std::cout << "       TESTING MO-ALNS EVRP SOLVER      \n";
  std::cout << "========================================\n";

  // 1. Kiểm tra tham số đầu vào (File path)
  std::string instancePath;
  std::string customRunName = "";
  unsigned int randomSeed = 0;

  if (argc > 1) {
    instancePath = argv[1];
  }
  if (argc >= 3) {
    customRunName = argv[2];
  }
  if (argc >= 4) {
    try {
      randomSeed = std::stoul(argv[3]);
    } catch (...) {
      std::cerr
          << "[Warning] Invalid random seed provided. Using time-based seed.\n";
      randomSeed = 0;
    }
  }

  if (argc == 1) {
    // Đường dẫn mặc định
    // instancePath = "../data/solomon/c101C5.txt";
    // instancePath = "../data/solomon/c101C10.txt";
    // instancePath = "../data/solomon/rc202C15.txt";
    // instancePath = "../data/solomon/c103C5.txt";
    // instancePath = "../data/solomon/c104C10.txt";
    // instancePath = "../data/solomon/rc108C15.txt";
    // instancePath = "../data/solomon/c208C15.txt";
    // instancePath = "../data/solomon/c101_21.txt";
    // instancePath = "../data/solomon/c102_21.txt";
    // instancePath = "../data/solomon/r107_21.txt";
    // instancePath = "../data/solomon/r105_21.txt";
    // instancePath = "../data/solomon/c106C15.txt";
    // instancePath = "../data/solomon/c104_21.txt";
    // instancePath = "../data/solomon/r109_21.txt";
    // instancePath = "../data/solomon/c103C15.txt";
    instancePath = "../data/solomon/c102_21.txt";
    // instancePath = "../data/solomon/c201_21.txt";
    // instancePath = "../data/solomon/rc108C15.txt";
    // instancePath = "../data/solomon/r201_21.txt";
    // instancePath = "../data/solomon/rc108C15.txt";
    // instancePath = "../data/solomon/rc103C15.txt";
    // instancePath = "../data/solomon/rc204C15.txt";
    // instancePath = "../data/solomon/r105C15.txt";
    // instancePath = "../data/solomon/r202_21.txt";
    // instancePath = "../data/solomon/r202C15.txt";
    // // instancePath = "../data/solomon/rc202_21.txt";
    // instancePath = "../data/solomon/rc103_21.txt";

    // instancePath = "../data/solomon/rc202C15.txt";

    std::cout << "[INFO] No instance file provided. Using default: "
              << instancePath << "\n";
  }

  try {
    // 2. Parse Instance
    std::cout << "[INFO] Loading instance from: " << instancePath << " ...\n";
    auto instance = Parser::parse(instancePath);

    if (!instance) {
      std::cerr << "[ERROR] Failed to parse instance!\n";
      return 1;
    }
    std::cout << "[INFO] Instance loaded successfully.\n";
    std::cout << "       - Customers: " << instance->getCustomers().size()
              << "\n";
    std::cout << "       - Stations:  " << instance->getStations().size()
              << "\n";

    // 3. Cấu hình ALNS (ALNSConfig)
    alns::ALNSConfig config;

    // --- Tham số cơ bản ---
    config.maxIterations = 25000;
    config.segmentIterations = 100;
    config.hvImprovementThreshold =
        0.0005;                    // ε = 0.05% (sensitive to small gains)
    config.hvStagnationLimit = 25; //

    // --- Adaptive Weights ---
    config.decayParameter = 0.85;
    config.scoreDominating = 40.0;
    config.scoreNonDominated = 25.0; // Increased from 25.0
    config.scoreDominated = 10.0;    // Increased from 10.0
    config.scoreIdentical = 0.0;

    // --- Destroy Params ---
    config.minRemoval = 0.15;
    config.maxRemoval =
        0.5; // Increased from 0.4 - destroy more for better exploration

    // --- Repair Params ---
    config.regretK = 3;
    config.noiseParameter = 0.5;

    // --- Local Search & Scatter Search ---
    config.useLocalSearch = false;
    config.useScatterSearch = false;
    config.localSearchIntensity =15; // Increased from 20

    // --- Scatter Search Params ---
    config.scatterSearchConfig.maxScatterIters = 5;
    config.scatterSearchConfig.alnsItersPerCombination = 50;

    // --- Simulated Annealing ---
    config.startTemperature =
        200.0; // Increased from 200.0 - CRITICAL for exploration
    config.coolingRate = 0.997; // Slower cooling from 0.995 - stay warm longer
    config.minTemperature =
        0.05; // Lower from 0.1 - allow smaller jumps at the end

    config.enableLogging = true;

    std::cout << "[INFO] ALNS Configured.\n";

    // ============================================================
    // [NEW] 4. TỰ ĐỘNG TẠO TÊN THƯ MỤC DỰA TRÊN TÊN FILE
    // ============================================================

    // Tạo đối tượng path từ đường dẫn đầu vào
    fs::path pathObj(instancePath);

    // Lấy tên file không có đuôi mở rộng (ví dụ: "c101C5.txt" -> "c101C5")
    std::string baseName = pathObj.stem().string();

    // Tạo đường dẫn thư mục output: logs/<TênFile>/<CustomRunName> (nếu có)
    std::string outputDir = "logs_testLS/" + baseName;
    std::string runName = baseName;

    if (!customRunName.empty()) {
      outputDir += "/" + customRunName;
      runName += "_" + customRunName;
    }

    // Set config random seed
    config.randomSeed = randomSeed;

    std::cout << "[INFO] Output Directory set to: " << outputDir << "\n";
    if (randomSeed > 0) {
      std::cout << "[INFO] Using Random Seed: " << randomSeed << "\n";
    }

    // Khởi tạo Solver với đường dẫn động
    auto solver = std::make_unique<alns::ALNSSolver>(instance, config,
                                                     outputDir, runName);

    // ============================================================

    // 5. Chạy thuật toán (Solve)
    std::cout << "[INFO] Starting Solver...\n";
    auto startTime = std::chrono::high_resolution_clock::now();

    std::vector<Solution> paretoFront = solver->solve();

    auto endTime = std::chrono::high_resolution_clock::now();
    long long duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                             endTime - startTime)
                             .count();

    // 6. Hiển thị kết quả
    std::cout << "\n========================================\n";
    std::cout << "           TEST COMPLETED               \n";
    std::cout << "========================================\n";
    std::cout << "Execution Time: " << duration << " ms\n";
    std::cout << "Pareto Front Size: " << paretoFront.size() << "\n";
    std::cout << "Results saved in: " << outputDir
              << "\n"; // Nhắc người dùng nơi lưu file

    if (!paretoFront.empty()) {
      // Sort Pareto Front by distance ascending for consistent reporting
      std::sort(paretoFront.begin(), paretoFront.end(),
                [](const Solution &a, const Solution &b) {
                  if (a.getTotalVehicles() != b.getTotalVehicles())
                    return a.getTotalVehicles() < b.getTotalVehicles();
                  return a.getTotalDistance() < b.getTotalDistance();
                });

      // === TABLE 1: Pareto Front Summary ===
      std::cout
          << "\n--- Pareto Front Summary (sorted by NV, then Distance) ---\n";
      std::cout << std::fixed << std::setprecision(2);
      std::cout << std::setw(4) << "#" << std::setw(6) << "NV" << std::setw(12)
                << "Distance" << std::setw(10) << "Gini" << std::setw(12)
                << "MaxTime" << std::setw(10) << "Feasible"
                << "\n";
      std::cout << std::string(54, '-') << "\n";
      int idx = 1;
      for (const auto &sol : paretoFront) {
        std::cout << std::setw(4) << idx++ << std::setw(6)
                  << sol.getTotalVehicles() << std::setw(12)
                  << sol.getTotalDistance() << std::setw(10)
                  << std::setprecision(4) << sol.getWorkloadGini()
                  << std::setw(12) << std::setprecision(2) << sol.getMaxTime()
                  << std::setw(10) << (sol.isFeasible() ? "YES" : "NO") << "\n";
      }
      std::cout << std::string(54, '-') << "\n";

      // === TABLE 2: Detailed Route Information for BEST Distance Solution ===
      std::cout
          << "\n--- Best Distance Solution (Solution #1) - Route Details ---\n";
      const auto &bestSol = paretoFront[0];
      printSolutionSummary(bestSol);

      int routeIdx = 1;
      for (const auto &route : bestSol.getRoutes()) {
        std::cout << route.toString();
        routeIdx++;
      }

      // === TABLE 3: Route Sequences for ALL Solutions (compact) ===
      std::cout << "\n--- Route Sequences (All Solutions) ---\n";
      idx = 1;
      for (const auto &sol : paretoFront) {
        std::cout << "\nSolution #" << idx++
                  << " (NV=" << sol.getTotalVehicles()
                  << ", Dist=" << std::setprecision(2) << sol.getTotalDistance()
                  << ")\n";
        routeIdx = 1;
        for (const auto &route : sol.getRoutes()) {
          std::cout << "  R" << routeIdx++ << ": ";
          const auto &nodes = route.getNodes();
          for (size_t i = 0; i < nodes.size(); ++i) {
            std::cout << nodes[i];
            if (i < nodes.size() - 1)
              std::cout << "-";
          }
          std::cout << " (d=" << std::setprecision(1)
                    << route.getTotalDistance()
                    << ", t=" << route.getTotalTime() << ")\n";
        }
      }

    } else {
      std::cout << "[WARNING] No feasible solution found!\n";
    }

  } catch (const std::exception &e) {
    std::cerr << "[EXCEPTION] " << e.what() << "\n";
    return 1;
  }

  return 0;
}