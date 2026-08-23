#include <chrono>
#include <filesystem>
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

// Hàm in kết quả (Helper)
void printSolutionSummary(const Solution &sol) {
  std::cout << "\n----------------------------------------\n";
  std::cout << "           SOLUTION SUMMARY             \n";
  std::cout << "----------------------------------------\n";
  std::cout << std::fixed << std::setprecision(2);
  std::cout << "Total Vehicles:   " << sol.getTotalVehicles() << "\n";
  std::cout << "Total Distance:   " << sol.getTotalDistance() << "\n";
  std::cout << "Workload Variance:" << sol.getWorkloadGini() << "\n";
  std::cout << "Max Time:         " << sol.getMaxTime() << "\n";
  std::cout << "Feasible:         " << (sol.isFeasible() ? "YES" : "NO")
            << "\n";
  std::cout << "----------------------------------------\n";
}

int main(int argc, char *argv[]) {
  // 1. Kiểm tra tham số đầu vào (File path)
  std::string instancePath;
  std::string customRunName = "";
  unsigned int randomSeed = 0;
  bool iraceMode = false;

  // 2. Cấu hình ALNS (ALNSConfig)
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
  config.SAScale = 10.0;
  config.maxTime = 285000; // thoi gian toi da chay 285 s = 285000 ms

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
    } else if (arg == "--hvIt" && i + 1 < argc) {
      config.hvImprovementThreshold = std::stod(argv[++i]);
    } else if (arg == "--hvLs" && i + 1 < argc) {
      config.hvStagnationLimit = std::stoi(argv[++i]);
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
    } else if (arg == "--sa scale" && i + 1 < argc) {
      config.SAScale = std::stod(argv[++i]);
    } else if (arg == "--runName" && i + 1 < argc) {
      customRunName = argv[++i];
    } else if (instancePath.empty() && arg.find("--") != 0) {
      // Hỗ trợ cách cũ: tham số đầu tiên là instance path nếu không dùng
      // --instance
      instancePath = arg;
    }
  }

  if (instancePath.empty()) {
    instancePath = "../data/solomon/c102_21.txt";
  }

  // ============================================================
  // [CẬP NHẬT TRÍCH XUẤT TÊN FILE SỚM]
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

  // ============================================================
  // [MẸO CHO R CONSOLE] - Bắn tín hiệu ra stderr để hiển thị realtime!
  // ============================================================
  if (!iraceMode) {
    // Dùng std::endl để force xả buffer ngay lập tức
    std::cerr << " >>> [RUNNING] Instance: " << baseName
              << " | Seed: " << randomSeed << std::endl;
  }

  try {
    // 3. Parse Instance
    auto instance = Parser::parse(instancePath);

    if (!instance) {
      if (!iraceMode)
        std::cerr << "[ERROR] Failed to parse instance " << baseName << "!\n";
      return 1;
    }

    // Khởi tạo Solver với đường dẫn động
    auto solver = std::make_unique<alns::ALNSSolver>(instance, config,
                                                     outputDir, runName);

    // 4. Chạy thuật toán (Solve)
    auto startTime = std::chrono::high_resolution_clock::now();

    std::vector<Solution> paretoFront = solver->solve();

    auto endTime = std::chrono::high_resolution_clock::now();
    long long duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                             endTime - startTime)
                             .count();

    // 5. Bắn tín hiệu Xong ra stderr cho R console biết
    if (!iraceMode) {
      std::cerr << " <<< [DONE] Instance: " << baseName
                << " | Seed: " << randomSeed << " | Time: " << duration << " ms"
                << std::endl;
    }

    // 6. Hiển thị kết quả ra stdout (Để R thu thập Dataframe sau này)
    if (iraceMode) {
      if (!paretoFront.empty()) {
        int bestNV = paretoFront[0].getTotalVehicles();
        for (const auto &sol : paretoFront) {
          if (sol.getTotalVehicles() < bestNV)
            bestNV = sol.getTotalVehicles();
        }
        double hv = solver->getHV();
        double cost = bestNV * 1000.0 - hv;
        std::cout << std::fixed << std::setprecision(6) << cost << std::endl;
      } else {
        std::cout << "99999999" << std::endl;
      }
    } else {
      // In kết quả chi tiết bình thường
      std::cout << "\n========================================\n";
      std::cout << "           TEST COMPLETED               \n";
      std::cout << "========================================\n";
      std::cout << "Instance: " << baseName << " | Seed: " << randomSeed
                << "\n";
      std::cout << "Execution Time: " << duration << " ms\n";
      std::cout << "Pareto Front Size: " << paretoFront.size() << "\n";
      std::cout << "Results saved in: " << outputDir << "\n";
      std::cout << "HV: " << solver->getHV() << "\n";

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
      std::cerr << "[EXCEPTION in " << baseName << "] " << e.what()
                << std::endl;
    else
      std::cout << "99999999" << std::endl;
    return 1;
  }

  return 0;
}