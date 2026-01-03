#include <iostream>
#include <memory>
#include <vector>
#include <string>
#include <chrono>
#include <iomanip>

#include "../../include/core/Instance.h"
#include "../../include/core/Solution.h"
#include "../../include/io/Parser.h"
#include "../../include/alns/ALNSSolver.h"
#include "../../include/utils/CmdLineParser.h" // Parser mới

/*
* =================================================================================
* TUNING APPLICATION
* =================================================================================
*
* Chức năng:
*   - Đây là một entry-point riêng biệt dành cho việc tinh chỉnh (tuning).
*   - Nó nhận TOÀN BỘ các tham số của ALNSConfig từ dòng lệnh.
*   - Output được thiết kế để cho script Python (tuning.py) dễ dàng đọc.
*
* Cách biên dịch:
*   - Thêm vào CMakeLists.txt: add_executable(TuningApp src/tuning/TuningApp.cpp)
*
* Cách chạy:
*   ./TuningApp <path_to_instance> --maxIterations 5000 --coolingRate 0.99 --...
*
*/

int main(int argc, char* argv[]) {
    // --- 1. Parse Command Line Arguments ---
    utils::CmdLineParser parser(argc, argv);

    std::string instancePath = parser.getUnnamedArg(0, "");
    if (instancePath.empty()) {
        std::cerr << "FATAL: Missing instance file path." << std::endl;
        std::cerr << "Usage: ./TuningApp <instance_path> [options...]" << std::endl;
        return 1;
    }

    // --- 2. Load Instance ---
    std::shared_ptr<Instance> instance;
    try {
        instance = Parser::parse(instancePath);
    } catch (const std::exception& e) {
        std::cerr << "FATAL: Could not parse instance file " << instancePath << ". Error: " << e.what() << std::endl;
        return 1;
    }

    // --- 3. Build ALNSConfig from Command Line ---
    alns::ALNSConfig config;

    // Lấy các giá trị từ parser, nếu không có thì dùng giá trị mặc định của config
    config.maxIterations = parser.get<int>("--maxIterations", config.maxIterations);
    config.segmentIterations = parser.get<int>("--segmentIterations", config.segmentIterations);
    config.maxIterationsWithoutImprovement = parser.get<int>("--maxIterationsWithoutImprovement", config.maxIterationsWithoutImprovement);

    config.decayParameter = parser.get<double>("--decayParameter", config.decayParameter);
    config.scoreDominating = parser.get<double>("--scoreDominating", config.scoreDominating);
    config.scoreNonDominated = parser.get<double>("--scoreNonDominated", config.scoreNonDominated);
    config.scoreDominated = parser.get<double>("--scoreDominated", config.scoreIdentical);
    config.scoreIdentical = parser.get<double>("--scoreIdentical", config.scoreIdentical);

    config.minRemoval = parser.get<double>("--minRemoval", config.minRemoval);
    config.maxRemoval = parser.get<double>("--maxRemoval", config.maxRemoval);

    config.regretK = parser.get<int>("--regretK", config.regretK);
    config.noiseParameter = parser.get<double>("--noiseParameter", config.noiseParameter);

    config.useLocalSearch = !parser.exists("--no-local-search"); // Bật trừ khi có cờ --no-local-search
    config.localSearchIntensity = parser.get<int>("--localSearchIntensity", config.localSearchIntensity);

    config.startTemperature = parser.get<double>("--startTemperature", config.startTemperature);
    config.coolingRate = parser.get<double>("--coolingRate", config.coolingRate);
    config.minTemperature = parser.get<double>("--minTemperature", config.minTemperature);

    // [NEW] Lấy các tham số logging. Logging sẽ chỉ được bật nếu cả 2 tham số này được cung cấp.
    std::string outputDir = parser.get<std::string>("--outputDir", "");
    std::string runName = parser.get<std::string>("--runName", "");
    config.enableLogging = !outputDir.empty() && !runName.empty();


    // --- 4. Initialize and Run Solver ---
    auto solver = std::make_unique<alns::ALNSSolver>(instance, config, outputDir, runName);

    std::vector<Solution> paretoFront;
    try {
        paretoFront = solver->solve();
    } catch (const std::exception& e) {
        std::cerr << "FATAL: An exception occurred during solve(): " << e.what() << std::endl;
        return 1;
    }

    // --- 5. Print Machine-Readable Output for Python Script ---
    // In kết quả ra stdout để script Python có thể bắt được
    if (config.enableLogging) {
        std::cout << "--- Run with logging enabled. See output files in " << outputDir << " ---" << std::endl;
    }
    std::cout << "--- FINAL PARETO FRONT ---" << std::endl;
    std::cout << std::fixed << std::setprecision(4);
    if (paretoFront.empty()) {
        std::cout << "No feasible solution found." << std::endl;
    } else {
        for (const auto& sol : paretoFront) {
            // Format: Solution #ID: Veh=X, Dist=Y, Workload=Z, MaxTime=W
            std::cout << "Solution #" << (&sol - &paretoFront[0] + 1) << ": "
                      << "Veh=" << sol.getTotalVehicles()
                      << ", Dist=" << sol.getTotalDistance()
                      << ", Workload=" << sol.getWorkloadVariance()
                      << ", MaxTime=" << sol.getMaxTime() << std::endl;
        }
    }

    return 0;
}
