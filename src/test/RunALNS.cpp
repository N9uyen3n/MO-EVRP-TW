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
  bool iraceMode = false;

  // 3. Cấu hình ALNS (ALNSConfig) - Move up to set values from CLI
  alns::ALNSConfig config;

  // --- Giá trị mặc định ---
  config.maxIterations = 25000;
  config.segmentIterations = 100;
  config.hvImprovementThreshold = 0.001;
  config.hvStagnationLimit = 5;
  config.decayParameter = 0.85;
  config.scoreDominating = 40.0;
  config.scoreNonDominated = 25.0;
  config.scoreDominated = 10.0;
  config.scoreIdentical = 0.0;
  config.minRemoval = 0.15;
  config.maxRemoval = 0.5;
  config.localSearchIntensity = 25;
  config.startTemperature = 200.0;
  config.coolingRate = 0.997;
  config.minTemperature = 0.05;
  
  config.useLocalSearch = true;
  config.useScatterSearch = true;
  config.enableLogging = true;

  // Parse command line arguments
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--irace") {
      iraceMode = true;
      config.enableLogging = false; // Tắt log file khi chạy irace
    } else if (arg == "--instance" && i + 1 < argc) {
      instancePath = argv[++i];
    } else if (arg == "--seed" && i + 1 < argc) {
      randomSeed = std::stoul(argv[++i]);
    } else if (arg == "--maxIter" && i + 1 < argc) {
      config.maxIterations = std::stoi(argv[++i]);
    } else if (arg == "--decay" && i + 1 < argc) {
      config.decayParameter = std::stod(argv[++i]);
    } else if (arg == "--score1" && i + 1 < argc) {
      config.scoreDominating = std::stod(argv[++i]);
    } else if (arg == "--score2" && i + 1 < argc) {
      config.scoreNonDominated = std::stod(argv[++i]);
    } else if (arg == "--score3" && i + 1 < argc) {
      config.scoreDominated = std::stod(argv[++i]);
    } else if (arg == "--minRem" && i + 1 < argc) {
      config.minRemoval = std::stod(argv[++i]);
    } else if (arg == "--maxRem" && i + 1 < argc) {
      config.maxRemoval = std::stod(argv[++i]);
    } else if (arg == "--startTemp" && i + 1 < argc) {
      config.startTemperature = std::stod(argv[++i]);
    } else if (arg == "--cool" && i + 1 < argc) {
      config.coolingRate = std::stod(argv[++i]);
    } else if (arg == "--runName" && i + 1 < argc) {
      customRunName = argv[++i];
    } else if (instancePath.empty() && arg.find("--") != 0) {
      // Hỗ trợ cách cũ: tham số đầu tiên là instance path nếu không dùng --instance
      instancePath = arg;
    }
  }

  if (instancePath.empty()) {
    instancePath = "../data/solomon/c102_21.txt";
    if (!iraceMode) {
      std::cout << "[INFO] No instance file provided. Using default: "
                << instancePath << "\n";
    }
  }

  if (!iraceMode) {
    std::cout << "========================================\n";
    std::cout << "       TESTING MO-ALNS EVRP SOLVER      \n";
    std::cout << "========================================\n";
  }

  try {
    // 2. Parse Instance
    if (!iraceMode)
      std::cout << "[INFO] Loading instance from: " << instancePath << " ...\n";
    auto instance = Parser::parse(instancePath);

    if (!instance) {
      if (!iraceMode)
        std::cerr << "[ERROR] Failed to parse instance!\n";
      return 1;
    }

    if (!iraceMode) {
      std::cout << "[INFO] Instance loaded successfully.\n";
      std::cout << "       - Customers: " << instance->getCustomers().size()
                << "\n";
      std::cout << "       - Stations:  " << instance->getStations().size()
                << "\n";
      std::cout << "[INFO] ALNS Configured.\n";
    }

    // ============================================================
    // [NEW] 4. TỰ ĐỘNG TẠO TÊN THƯ MỤC DỰA TRÊN TÊN FILE
    // ============================================================

    fs::path pathObj(instancePath);
    std::string baseName = pathObj.stem().string();
    std::string outputDir = "logs/" + baseName;
    std::string runName = baseName;

    if (!customRunName.empty()) {
      outputDir += "/" + customRunName;
      runName += "_" + customRunName;
    }

    config.randomSeed = randomSeed;

    if (!iraceMode) {
      std::cout << "[INFO] Output Directory set to: " << outputDir << "\n";
      if (randomSeed > 0) {
        std::cout << "[INFO] Using Random Seed: " << randomSeed << "\n";
      }
    }

    // Khởi tạo Solver với đường dẫn động
    auto solver = std::make_unique<alns::ALNSSolver>(instance, config,
                                                     outputDir, runName);

    // 5. Chạy thuật toán (Solve)
    if (!iraceMode)
      std::cout << "[INFO] Starting Solver...\n";
    auto startTime = std::chrono::high_resolution_clock::now();

    std::vector<Solution> paretoFront = solver->solve();

    auto endTime = std::chrono::high_resolution_clock::now();
    long long duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                             endTime - startTime)
                             .count();

    // 6. Hiển thị kết quả
    if (iraceMode) {
      // Chế độ irace: chỉ in ra giá trị mục tiêu (ví dụ: quãng đường của giải pháp đầu tiên)
      // Nếu là đa mục tiêu, irace thường cần 1 giá trị duy nhất, ở đây ta lấy Best Distance.
      if (!paretoFront.empty()) {
        // Tìm giải pháp có quãng đường nhỏ nhất trong Pareto Front
        double bestDist = paretoFront[0].getTotalDistance();
        for (const auto &sol : paretoFront) {
          if (sol.getTotalDistance() < bestDist)
            bestDist = sol.getTotalDistance();
        }
        std::cout << std::fixed << std::setprecision(4) << bestDist << std::endl;
      } else {
        std::cout << "99999999" << std::endl; // Giá trị phạt nếu không tìm thấy KQ
      }
    } else {
      std::cout << "\n========================================\n";
      std::cout << "           TEST COMPLETED               \n";
      std::cout << "========================================\n";
      std::cout << "Execution Time: " << duration << " ms\n";
      std::cout << "Pareto Front Size: " << paretoFront.size() << "\n";
      std::cout << "Results saved in: " << outputDir << "\n";

      if (!paretoFront.empty()) {
        std::cout << "\n--- Best Solutions Found (Pareto Front) ---\n";
        std::cout << std::fixed << std::setprecision(2);
        int idx = 1;
        for (const auto &sol : paretoFront) {
          std::cout << "Solution #" << idx++ << ": "
                    << "Veh=" << sol.getTotalVehicles()
                    << ", Dist=" << sol.getTotalDistance()
                    << ", Fairness=" << sol.getWorkloadGini()
                    << ", MaxTime=" << sol.getMaxTime() << "\n";
        }

        std::cout << "\n--- Details of Solution #1 ---\n";
        printSolutionSummary(paretoFront[0]);
      } else {
        std::cout << "[WARNING] No feasible solution found!\n";
      }
    }

  } catch (const std::exception &e) {
    if (!iraceMode)
      std::cerr << "[EXCEPTION] " << e.what() << "\n";
    else
      std::cout << "99999999" << std::endl;
    return 1;
  }

  return 0;
}