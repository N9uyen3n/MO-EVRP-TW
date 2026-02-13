#include <chrono>
#include <filesystem> // [NEW] Thêm thư viện này để xử lý đường dẫn file
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
  std::cout << "Workload Variance:" << sol.getWorkloadVariance() << "\n";
  std::cout << "Gini Coefficient: " << sol.getGiniCoefficient()
            << "\n"; // ⭐ NEW
  std::cout << "Max Time:         " << sol.getMaxTime() << "\n";
  std::cout << "Feasible:         " << (sol.isFeasible() ? "YES" : "NO")
            << "\n";
  std::cout << "----------------------------------------\n";
}

int main(int argc, char *argv[]) {
  std::cout << "========================================\n";
  std::cout << "       TESTING MO-ALNS EVRP SOLVER      \n";
  std::cout << "========================================\n";

  // 1. Parse Command Line Arguments
  std::string instancePath;
  std::string modeStr = "ADAPTIVE_FAIRNESS"; // Default
  std::string outputDirOverride = "";

  std::cout << "[DEBUG] argc: " << argc << "\n";
  for (int i = 0; i < argc; ++i) {
    std::cout << "[DEBUG] argv[" << i << "]: " << argv[i] << "\n";
  }

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "--instance" && i + 1 < argc) {
      instancePath = argv[++i];
    } else if (arg == "--mode" && i + 1 < argc) {
      modeStr = argv[++i];
    } else if (arg == "--output" && i + 1 < argc) {
      outputDirOverride = argv[++i];
    } else if (arg.find("--") != 0) {
      // Backward compatibility: first non-flag arg is instance path
      if (instancePath.empty()) {
        instancePath = arg;
      }
    }
  }

  if (instancePath.empty()) {
    // Đường dẫn mặc định
    // instancePath = "../data/solomon/c101C5.txt";
    // instancePath = "../data/solomon/c101C10.txt";
    // instancePath = "../data/solomon/rc204C15.txt";
    // instancePath = "../data/solomon/c103C5.txt";
    // instancePath = "../data/solomon/c104C10.txt";
    // instancePath = "../data/solomon/rc108C15.txt";
    // instancePath = "../data/solomon/c208C15.txt";
    // instancePath = "../data/solomon/c101_21.txt";
    // instancePath = "../data/solomon/c102_21.txt";
    // instancePath = "../data/solomon/r107_21.txt";
    // instancePath = "../data/solomon/c106C15.txt";
    // instancePath = "../data/solomon/c104_21.txt";
    // instancePath = "../data/solomon/r109_21.txt";
    // instancePath = "../data/solomon/c103C15.txt";
    instancePath = "../data/solomon/r201_21.txt";
    // instancePath = "../data/solomon/rc108C15.txt";
    // instancePath = "../data/solomon/rc103C15.txt";
    // instancePath = "../data/solomon/rc204C15.txt";
    // instancePath = "../data/solomon/r105C15.txt";

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
    config.maxIterations = 30000; // Increased from 25000
    config.segmentIterations = 100;
    config.maxIterationsWithoutImprovement = 2000; // Increased from 3000

    // --- Adaptive Weights ---
    config.decayParameter = 0.85;
    config.scoreDominating =
        50.0; // Increased from 40.0 - reward good operators more
    config.scoreNonDominated = 30.0; // Increased from 25.0
    config.scoreDominated = 15.0;    // Increased from 10.0
    config.scoreIdentical = 0.0;

    // --- Destroy Params ---
    config.minRemoval = 0.15;
    config.maxRemoval =
        0.5; // Increased from 0.4 - destroy more for better exploration

    // --- Repair Params ---
    config.regretK = 3;
    config.noiseParameter = 0.5;

    // --- Local Search ---
    config.useLocalSearch = true;
    config.localSearchIntensity = 25; // Increased from 20

    // --- Simulated Annealing ---
    config.startTemperature =
        200.0; // Increased from 200.0 - CRITICAL for exploration
    config.coolingRate = 0.997; // Slower cooling from 0.995 - stay warm longer
    config.minTemperature =
        0.05; // Lower from 0.1 - allow smaller jumps at the end

    config.enableLogging = true;

    // ⭐ NEW: Configure Fairness Mode
    if (modeStr == "COST_ONLY") {
      config.fairnessMode = alns::ALNSConfig::COST_ONLY;
      std::cout << "[INFO] Fairness Mode: COST_ONLY (Baseline A)\n";
    } else if (modeStr == "STATIC_FAIRNESS") {
      config.fairnessMode = alns::ALNSConfig::STATIC_FAIRNESS;
      std::cout << "[INFO] Fairness Mode: STATIC_FAIRNESS (Baseline B)\n";
    } else if (modeStr == "ADAPTIVE_FAIRNESS" || modeStr == "ADAPTIVE") {
      config.fairnessMode = alns::ALNSConfig::ADAPTIVE_FAIRNESS;
      std::cout
          << "[INFO] Fairness Mode: ADAPTIVE_FAIRNESS (Proposed Method)\n";
    } else {
      std::cerr << "[WARNING] Unknown mode: " << modeStr
                << ". Using ADAPTIVE_FAIRNESS.\n";
      config.fairnessMode = alns::ALNSConfig::ADAPTIVE_FAIRNESS;
    }

    std::cout << "[INFO] ALNS Configured.\n";

    // ============================================================
    // [NEW] 4. TỰ ĐỘNG TẠO TÊN THƯ MỤC DỰA TRÊN TÊN FILE
    // ============================================================

    // Tạo đối tượng path từ đường dẫn đầu vào
    fs::path pathObj(instancePath);

    // Lấy tên file không có đuôi mở rộng (ví dụ: "c101C5.txt" -> "c101C5")
    std::string baseName = pathObj.stem().string();

    // Tạo đường dẫn thư mục output: logs/<TênFile>
    std::string outputDir = "logs/" + baseName;
    std::string runName = baseName;

    // Override if specified via command line
    if (!outputDirOverride.empty()) {
      outputDir = outputDirOverride;
      std::cout << "[INFO] Using custom output directory: " << outputDir
                << "\n";
    } else {
      std::cout << "[INFO] Output Directory set to: " << outputDir << "\n";
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
      std::cout << "\n--- Best Solutions Found (Pareto Front) ---\n";
      std::cout << std::fixed << std::setprecision(2);
      int idx = 1;
      for (const auto &sol : paretoFront) {
        std::cout << "Solution #" << idx++ << ": "
                  << "Veh=" << sol.getTotalVehicles()
                  << ", Dist=" << sol.getTotalDistance()
                  << ", Gini=" << sol.getGiniCoefficient()
                  << ", Workload=" << sol.getWorkloadVariance()
                  << ", MaxTime=" << sol.getMaxTime() << "\n";
      }

      std::cout << "\n--- Details of Solution #1 ---\n";
      printSolutionSummary(paretoFront[0]);
    } else {
      std::cout << "[WARNING] No feasible solution found!\n";
    }

  } catch (const std::exception &e) {
    std::cerr << "[EXCEPTION] " << e.what() << "\n";
    return 1;
  }

  return 0;
}