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

// Hàm in kết quả Pareto
void printParetoFront(const std::vector<Solution> &front) {
  if (front.empty()) {
    std::cout << "[WARNING] Pareto front is empty!\n";
    return;
  }

  std::cout << "\n========================================\n";
  std::cout << "       PARETO FRONT SOLUTIONS           \n";
  std::cout << "========================================\n";
  std::cout << std::fixed << std::setprecision(2);
  std::cout << std::left << std::setw(10) << "Sol #" << std::setw(10)
            << "Vehicles" << std::setw(15) << "Distance" << std::setw(15)
            << "Gini Coeff" << std::setw(15) << "Balance (Var)" << "\n";
  std::cout << "------------------------------------------------------------\n";

  int idx = 1;
  for (const auto &sol : front) {
    std::cout << std::left << std::setw(10) << idx++ << std::setw(10)
              << sol.getTotalVehicles() << std::setw(15)
              << sol.getTotalDistance() << std::setw(15)
              << sol.getGiniCoefficient() << std::setw(15)
              << sol.getWorkloadVariance() << "\n";
  }
  std::cout << "========================================\n";
}

int main(int argc, char *argv[]) {
  std::cout << "========================================\n";
  std::cout << "       PARETO DEMO - MO-ALNS EVRP       \n";
  std::cout << "========================================\n";

  // 1. Xác định file instance
  std::string instancePath = "../data/solomon/C101_21.txt"; // Default hardcoded
  if (argc > 1) {
    instancePath = argv[1];
  } else {
    std::cout << "[INFO] No instance provided. Using default: " << instancePath
              << "\n";
    std::cout << "Usage: ./ParetoDemo <path_to_instance>\n";
  }

  // 2. Load Instance
  try {
    std::cout << "[INFO] Loading instance: " << instancePath << " ...\n";
    auto instance = Parser::parse(instancePath);
    if (!instance) {
      std::cerr << "[ERROR] Failed to parse instance.\n";
      return 1;
    }
    std::cout << "[INFO] Loaded: " << instance->getCustomers().size()
              << " customers, " << instance->getStations().size()
              << " stations.\n";

    // 3. Cấu hình ALNS (Focus vào tìm kiếm Pareto)
    alns::ALNSConfig config;

    // Chạy ít vòng lặp hơn để demo nhanh nhưng đủ để hội tụ một chút
    config.maxIterations = 5000;
    config.maxIterationsWithoutImprovement = 1000;
    config.segmentIterations = 100;

    // Bật chế độ Adaptive Fairness để sinh ra Pareto Front
    config.fairnessMode = alns::ALNSConfig::ADAPTIVE_FAIRNESS;

    // Các tham số khác
    config.decayParameter = 0.85;
    config.startTemperature = 100.0;
    config.coolingRate = 0.99;

    // Tắt logging chi tiết để output gọn gàng
    config.enableLogging = false;

    std::cout << "[INFO] Configured ALNS with ADAPTIVE_FAIRNESS mode.\n";
    std::cout << "[INFO] Running Solver (5000 iterations)...\n";

    // 4. Giải
    auto solver = std::make_unique<alns::ALNSSolver>(
        instance, config, "logs/pareto_demo", "demo_run");
    auto startTime = std::chrono::high_resolution_clock::now();

    std::vector<Solution> paretoFront = solver->solve();

    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                        endTime - startTime)
                        .count();

    std::cout << "[INFO] Solver finished in " << duration << " ms.\n";

    // 5. In kết quả
    printParetoFront(paretoFront);

  } catch (const std::exception &e) {
    std::cerr << "[EXCEPTION] " << e.what() << "\n";
    return 1;
  }

  return 0;
}
