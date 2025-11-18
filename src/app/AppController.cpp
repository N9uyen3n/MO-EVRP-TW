// app/AppController.cpp
#include "../../include/app/AppController.h"
#include "../../include/io/Parser.h" // Cần để load instance
#include "../../include/utils/Utils.h" // Cần để print instance
// --- Import tất cả các toán tử bạn cần ---
// Destroy
#include "../../include/alns/operators/destroy/RandomDestroy.h"
#include "../../include/alns/operators/destroy/ShawDestroy.h"
#include "../../include/alns/operators/destroy/WorstDestroy.h"
#include "../../include/alns/operators/destroy/RouteRemoval.h"
// Repair
#include "../../include/alns/operators/repair/GreedyRepair.h"
#include "../../include/alns/operators/repair/RegretKRepair.h"
#include "../../include/alns/operators/repair/GreedyStationRepair.h"

#include <iostream>
#include <stdexcept> // Cần cho std::runtime_error

// --- Constructor ---
AppController::AppController(const std::string& instancePath,
                             const std::string& outputDir,
                             const std::string& runName)
    : instancePath(instancePath), outputDir(outputDir), runName(runName)
{
    // --- 1. Tải Dữ liệu (Instance) ---
    // (Logic này được chuyển từ main() cũ vào constructor)
    std::cout << "Parsing instance file: " << instancePath << std::endl;
    this->instance = Parser::parse(this->instancePath);
    if (!this->instance) {
        throw std::runtime_error("Failed to parse instance file.");
    }
    Utils::printInstance(*(this->instance));
}

// --- Hàm Run chính ---
void AppController::run() {
    // --- 2. Cấu hình ALNS (ALNSConfig) ---
    ALNSConfig config = createConfig();

    // --- 3. Khởi tạo ALNSSolver ---
    ALNSSolver solver(this->instance, config, this->outputDir, this->runName);
    std::cout << "Create initial ALNS solution successfully." << std::endl;

    // --- 4. Đăng ký (Register) các Toán tử ---
    registerOperators(solver, config);

    // --- 5. Chạy Thuật toán ---
    std::cout << "---  Start Solve()  ---" << std::endl;
    std::vector<Solution> finalFront = solver.solve();

    std::cout << "--- Solve() Finished ---" << std::endl;
    std::cout << "Pareto Size: " << finalFront.size() << std::endl;
}


// --- Hàm Helper 1: Tạo Config ---
ALNSConfig AppController::createConfig() {
    // (Toàn bộ logic config từ TestALNS.cpp được chuyển vào đây)
    ALNSConfig config;

    // Tham số Vòng lặp
    config.maxIterations = 2000;
    config.segmentIterations = 50;
    config.maxIterationsWithoutImprovement = 500;

    // Tham số Điểm (Scores)
    config.decayParameter = 0.8;
    config.scoreDominating = 35.0;
    config.scoreNonDominated = 15.0;
    config.scoreDominated = 5.0;
    config.scoreIdentical = 0.0;

    // Tham số Destroy/Repair
    config.minRemoval = 0.2;
    config.maxRemoval = 0.4;
    config.regretK = 2;
    config.noiseParameter = 0.2;

    // Tham số Local Search
    config.useLocalSearch = true;
    config.localSearchIntensity = 3;
    config.startTemperature = 100.0;
    config.coolingRate = 0.995;
    config.minTemperature = 0.1;

    // Logging
    config.enableLogging = true;

    return config;
}

// --- Hàm Helper 2: Đăng ký Operators ---
void AppController::registerOperators(ALNSSolver& solver, const ALNSConfig& config) {
    // (Toàn bộ logic đăng ký operators từ TestALNS.cpp được chuyển vào đây)

    // Cấu hình ShawDestroy
    ShawDestroy::RelatednessWeights weights;
    weights.distanceWeight = 5.0;
    weights.timeWindowWeight = 2.0;
    weights.demandWeight = 1.0;
    weights.routeWeight = 1.0;

    // Destroy
    solver.addDestroyOperator(std::make_shared<RandomDestroy>(), 1.0);
    solver.addDestroyOperator(std::make_shared<ShawDestroy>(this->instance, weights, 3), 1.0);
    solver.addDestroyOperator(std::make_shared<WorstDestroy>(this->instance, 3), 1.0);
    solver.addDestroyOperator(std::make_shared<RouteRemoval>(this->instance), 1.0);

    // Repair
    solver.addRepairOperator(std::make_shared<GreedyRepair>(this->instance), 1.0);
    solver.addRepairOperator(std::make_shared<RegretKRepair>(this->instance, config.regretK, config.noiseParameter), 1.0);
    solver.addRepairOperator(std::make_shared<GreedyStationRepair>(this->instance), 1.0);

    std::cout << "Register 4 Destroy and 3 Repair operators." << std::endl;
}