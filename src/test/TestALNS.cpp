#include <iostream>
#include <memory>
#include <vector>
#include <string>
#include <chrono>
#include <filesystem> // [NEW] Thêm thư viện này để xử lý đường dẫn file

// --- Core ---
#include "../../include/core/Instance.h"
#include "../../include/core/Solution.h"
#include "../../include/io/Parser.h"

// --- ALNS ---
#include "../../include/alns/ALNSSolver.h"

namespace fs = std::filesystem; // [NEW] Alias cho ngắn gọn

// Hàm in kết quả (Helper)
void printSolutionSummary(const Solution& sol) {
    std::cout << "\n----------------------------------------\n";
    std::cout << "           SOLUTION SUMMARY             \n";
    std::cout << "----------------------------------------\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Total Vehicles:   " << sol.getTotalVehicles() << "\n";
    std::cout << "Total Distance:   " << sol.getTotalDistance() << "\n";
    std::cout << "Workload Variance:" << sol.getWorkloadVariance() << "\n";
    std::cout << "Max Time:         " << sol.getMaxTime() << "\n";
    std::cout << "Feasible:         " << (sol.isFeasible() ? "YES" : "NO") << "\n";
    std::cout << "----------------------------------------\n";
}

int main(int argc, char* argv[]) {
    std::cout << "========================================\n";
    std::cout << "       TESTING MO-ALNS EVRP SOLVER      \n";
    std::cout << "========================================\n";

    // 1. Kiểm tra tham số đầu vào (File path)
    std::string instancePath;
    if (argc > 1) {
        instancePath = argv[1];
    } else {
        // Đường dẫn mặc định
        // instancePath = "data/solomon/c101C5.txt";
        // instancePath = "data/solomon/c101C10.txt";
        // instancePath = "data/solomon/rc204C15.txt";
        // instancePath = "data/solomon/c103C5.txt";
        // instancePath = "data/solomon/c208C15.txt";
        // instancePath = "data/solomon/c101_21.txt";
        instancePath = "data/solomon/rc202C15.txt";


        std::cout << "[INFO] No instance file provided. Using default: " << instancePath << "\n";
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
        std::cout << "       - Customers: " << instance->getCustomers().size() << "\n";
        std::cout << "       - Stations:  " << instance->getStations().size() << "\n";

        // 3. Cấu hình ALNS (ALNSConfig)
        alns::ALNSConfig config;

        // --- Tham số cơ bản ---
        config.maxIterations = 1000;
        config.segmentIterations = 100;
        config.maxIterationsWithoutImprovement = 700;

        // --- Adaptive Weights ---
        config.decayParameter = 0.7;
        config.scoreDominating = 35.0;
        config.scoreNonDominated = 15.0;
        config.scoreDominated = 5.0;
        config.scoreIdentical = 0.0;

        // --- Destroy Params ---
        config.minRemoval = 0.1;
        config.maxRemoval = 0.4;

        // --- Repair Params ---
        config.regretK = 2;
        config.noiseParameter = 0.1;

        // --- Local Search ---
        config.useLocalSearch = true;
        config.localSearchIntensity = 30;

        // --- Simulated Annealing ---
        config.startTemperature = 100.0;
        config.coolingRate = 0.99;
        config.minTemperature = 0.1;

        config.enableLogging = true;

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

        std::cout << "[INFO] Output Directory set to: " << outputDir << "\n";

        // Khởi tạo Solver với đường dẫn động
        auto solver = std::make_unique<alns::ALNSSolver>(instance, config, outputDir, runName);

        // ============================================================

        // 5. Chạy thuật toán (Solve)
        std::cout << "[INFO] Starting Solver...\n";
        auto startTime = std::chrono::high_resolution_clock::now();

        std::vector<Solution> paretoFront = solver->solve();

        auto endTime = std::chrono::high_resolution_clock::now();
        long long duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

        // 6. Hiển thị kết quả
        std::cout << "\n========================================\n";
        std::cout << "           TEST COMPLETED               \n";
        std::cout << "========================================\n";
        std::cout << "Execution Time: " << duration << " ms\n";
        std::cout << "Pareto Front Size: " << paretoFront.size() << "\n";
        std::cout << "Results saved in: " << outputDir << "\n"; // Nhắc người dùng nơi lưu file

        if (!paretoFront.empty()) {
            std::cout << "\n--- Best Solutions Found (Pareto Front) ---\n";
            std::cout << std::fixed << std::setprecision(2); // Set precision for cleaner output
            int idx = 1;
            for (const auto& sol : paretoFront) {
                std::cout << "Solution #" << idx++ << ": "
                          << "Veh=" << sol.getTotalVehicles()
                          << ", Dist=" << sol.getTotalDistance()
                          << ", Workload=" << sol.getWorkloadVariance()
                          << ", MaxTime=" << sol.getMaxTime() << "\n";
            }

            std::cout << "\n--- Details of Solution #1 ---\n";
            printSolutionSummary(paretoFront[0]);
        } else {
            std::cout << "[WARNING] No feasible solution found!\n";
        }

    } catch (const std::exception& e) {
        std::cerr << "[EXCEPTION] " << e.what() << "\n";
        return 1;
    }

    return 0;
}