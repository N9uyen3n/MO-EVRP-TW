#include <iostream>
#include <string>
#include <memory>
#include <stdexcept> // Dùng cho try...catch

// --- Core ---
#include "core/Instance.h"
#include "core/Solution.h"
#include "core/Depot.h"
#include "core/Customer.h" // <-- SỬA LỖI 1: THIẾU INCLUDE
#include "utils/Utils.h"

// --- IO ---
#include "io/Parser.h"


// --- ALNS ---
#include "alns/ALNSSolver.h"

// --- Operators (Import tất cả các toán tử bạn đã viết) ---
// Destroy
#include "alns/operators/destroy/RandomDestroy.h"
#include "alns/operators/destroy/ShawDestroy.h"
#include "alns/operators/destroy/WorstDestroy.h"
// Repair
#include "alns/operators/destroy/RouteRemoval.h"
#include "alns/operators/repair/GreedyRepair.h"
#include "alns/operators/repair/RegretKRepair.h"
#include "alns/operators/repair/GreedyStationRepair.h"


int main() {
    std::cout << "---  Start  TestALNS ---" << std::endl;

    try {
        // --- 1. Tải Dữ liệu (Instance) ---
        // std::string instancePath = "data/solomon/c101C10.txt";
        // std::string instanceName = "c101C10_Test"; // Tên này dùng để đặt tên file log

        // std::string instancePath = "data/solomon/c103C5.txt";
        // std::string instanceName = "c103C5_Test"; // Tên này dùng để đặt tên file log

        // std::string instancePath = "data/solomon/c103C15.txt";
        // std::string instanceName = "c103C15_Test"; // Tên này dùng để đặt tên file log

        // std::string instancePath = "data/solomon/r209C15.txt";
        // std::string instanceName = "r209C15_Test"; // Tên này dùng để đặt tên file log

        // std::string instancePath = "data/solomon/rc202C15.txt";
        // std::string instanceName = "rc202C15_Test"; // Tên này dùng để đặt tên file log

        std::string instancePath = "data/solomon/c101C5.txt";
        std::string instanceName = "c101C5_Test"; // Tên này dùng để đặt tên file log

        // std::string instancePath = "data/solomon/r201C10.txt";
        // std::string instanceName = "r201C10_Test"; // Tên này dùng để đặt tên file log


        std::shared_ptr<Instance> instance = Parser::parse(instancePath);
        Utils::printInstance(*instance);


        // --- 2. Cấu hình ALNS (ALNSConfig) ---
        ALNSConfig config;

        // Tham số Vòng lặp
        config.maxIterations = 3000;
        config.segmentIterations = 50;
        config.maxIterationsWithoutImprovement = 500; // Thêm tham số dừng sớm

        // Tham số Điểm (Scores)
        config.decayParameter = 0.8;
        config.scoreDominating = 30.0;
        config.scoreNonDominated = 20.0;
        config.scoreDominated = 5.0;
        config.scoreIdentical = 0.0;

        // Tham số Destroy/Repair
        config.minRemoval = 0.2;
        config.maxRemoval = 0.4;
        config.regretK = 3;
        config.noiseParameter = 0.1;

        // Tham số Local Search
        config.useLocalSearch = true; // Bật LS
        config.localSearchIntensity = 1.0;

        // Đây là mấu chốt để thuật toán thoát khỏi "bị kẹt" (stuck)
        config.startTemperature = 100.0;  // Nhiệt độ ban đầu cao
        config.coolingRate = 0.995;     // Làm nguội từ từ
        config.minTemperature = 0.1;      // Nhiệt độ sàn

        // Logging
        config.enableLogging = true;


        // --- 3. Cấu hình Logger ---
        std::string outputDir = "results/" + instanceName;
        std::string runName = "run_1_base_config";


        // --- 4. Khởi tạo ALNSSolver ---
        ALNSSolver solver(instance, config, outputDir, runName);
        std::cout << "Create initial ALNS solution successfully." << std::endl;


        // --- 5. Đăng ký (Register) các Toán tử ---
        // Destroy
        ShawDestroy::RelatednessWeights weights;
        weights.distanceWeight = 5.0;     // Ưu tiên khoảng cách
        weights.timeWindowWeight = 2.0;   // Ưu tiên thời gian
        weights.demandWeight = 1.0;       // Ít ưu tiên nhu cầu
        weights.routeWeight = 1.0;        // Ưu tiên việc ở chung/khác tuyến

        // Destroy
        // solver.addDestroyOperator(std::make_shared<RandomDestroy>(), 1.0);

        // SỬA LỖI: Truyền trọng số (weights) VÀ determinism (3)
        solver.addDestroyOperator(std::make_shared<ShawDestroy>(instance, weights, 3), 1.0);
        // ĐÚNG: Giữ nguyên
        solver.addDestroyOperator(std::make_shared<WorstDestroy>(instance, 3), 1.0);
        solver.addDestroyOperator(std::make_shared<RouteRemoval>(instance), 1.0);

        // Repair
        // solver.addRepairOperator(std::make_shared<GreedyRepair>(instance), 1.0);
        solver.addRepairOperator(std::make_shared<RegretKRepair>(instance, config.regretK, config.noiseParameter), 1.0);
        solver.addRepairOperator(std::make_shared<GreedyStationRepair>(instance), 1.0);
        
        std::cout << "Register 3 Destroy and 3 Repair operators." << std::endl;

        
        // --- 6. Chạy Thuật toán ---
        std::cout << "---  Start Solve()  ---" << std::endl;
        std::vector<Solution> finalFront = solver.solve();
        
        std::cout << "--- Solve() Finished ---" << std::endl;
        std::cout << "Pareto Size: " << finalFront.size() << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Critical Error: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "--- TestALNS END ---" << std::endl;
    return 0;
}