#include "alns/ALNSSolver.h"

// SỬA ĐỔI: Bao gồm các file logger chính xác theo thiết kế mới
#include "logger/ComprehensiveLogger.h"
#include "logger/NullLogger.h"

#include "alns/ParetoArchive.h" // Cần thiết
#include "core/Customer.h"      // Cần cho dynamic_cast
#include <iostream>
#include <chrono>
#include <numeric>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <iomanip>
#include <random>

// ******************************************************************
// ** 1. TRIỂN KHAI CÁC HÀM CỦA HELPER STRUCT `OperatorPool`
// ** (Phần này của bạn đã đúng, giữ nguyên)
// ******************************************************************

int OperatorPool::select(std::mt19937& rng) {
    double totalWeight = std::accumulate(weights.begin(), weights.end(), 0.0);
    if (totalWeight <= 1e-9) {
        std::uniform_int_distribution<> dist(0, operators.size() - 1);
        return dist(rng);
    }
    std::uniform_real_distribution<> dist(0.0, totalWeight);
    double r = dist(rng);
    double currentSum = 0.0;
    for (size_t i = 0; i < operators.size(); ++i) {
        currentSum += weights[i];
        if (r <= currentSum) {
            return i;
        }
    }
    return operators.size() - 1;
}

void OperatorPool::updateWeights(double decay) {
    for (size_t i = 0; i < operators.size(); ++i) {
        double avgScore = (usages[i] > 0) ? (scores[i] / usages[i]) : 0.0;
        weights[i] = weights[i] * decay + (1.0 - decay) * avgScore;
        weights[i] = std::max(0.1, weights[i]); // Giữ trọng số tối thiểu
    }
}

void OperatorPool::resetScores() {
    std::fill(scores.begin(), scores.end(), 0.0);
    std::fill(usages.begin(), usages.end(), 0);
}


// ******************************************************************
// ** 2. TRIỂN KHAI CONSTRUCTOR / DESTRUCTOR CỦA `ALNSSolver`
// ** (Sửa đổi hoàn toàn để tuân thủ thiết kế ILogger)
// ******************************************************************

ALNSSolver::ALNSSolver(std::shared_ptr<Instance> instance, ALNSConfig config,
                       const std::string& outputDirectory, const std::string& runName)
    : instance(instance),
        s_current(instance),
      config(config),
      archive(100), // Bạn có thể thêm 'archiveMaxSize' vào ALNSConfig
      localSearch(instance),
    dist_0_1(0.0, 1.0)

{
    // 1. Khởi tạo bộ tạo số ngẫu nhiên
    unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
    this->rng.seed(seed);

    // 2. Đây là công tắc BẬT/TẮT logging
    if (config.enableLogging) {
        // Bật: Tạo Logger Toàn diện
        this->logger = std::make_unique<logging::ComprehensiveLogger>(
                           outputDirectory, runName);
        std::cout << "Logging ENABLED. (Writing full logs to: "
                  << outputDirectory << ")" << std::endl;
    } else {
        // Tắt: Tạo Logger Rỗng
        this->logger = std::make_unique<logging::NullLogger>();
        std::cout << "Logging DISABLED. (Running at max performance)" << std::endl;
    }

    // 3. Ghi log config (nếu Logger là Null, hàm này không làm gì cả)
    this->logger->logConfig(this->config);
    for (const auto& node : instance->getNodes()) {
        if (std::dynamic_pointer_cast<Customer>(node)) {
            this->totalCustomers++;
        }
    }

    this->currentTemperature = config.startTemperature;
    randomEngine.seed(std::chrono::system_clock::now().time_since_epoch().count());
}

// Destructor (giữ nguyên, vì nó trỏ đến ILogger)
ALNSSolver::~ALNSSolver() = default;


// ******************************************************************
// ** 3. TRIỂN KHAI CÁC HÀM PUBLIC
// ** (add...Operator giữ nguyên, solve() được cập nhật)
// ******************************************************************

void ALNSSolver::addDestroyOperator(std::shared_ptr<IDestroyOperator> op, double initialWeight) {
    destroyPool.operators.push_back(op);
    destroyPool.weights.push_back(initialWeight);
    destroyPool.scores.push_back(0.0);
    destroyPool.usages.push_back(0);
}

void ALNSSolver::addRepairOperator(std::shared_ptr<IRepairOperator> op, double initialWeight) {
    repairPool.operators.push_back(op);
    repairPool.weights.push_back(initialWeight);
    repairPool.scores.push_back(0.0);
    repairPool.usages.push_back(0);
}

/**
 * @brief Hàm chính, chạy toàn bộ thuật toán ALNS. (Đã cập nhật)
 */
// std::vector<Solution> ALNSSolver::solve() {
//     if (destroyPool.operators.empty() || repairPool.operators.empty()) {
//         throw std::runtime_error("ALNS Solver cannot run without at least one destroy and one repair operator.");
//     }
//
//     auto startTime = std::chrono::high_resolution_clock::now();
//     std::cout << "ALNS Started..." << std::endl;
//
//     // 1. Tạo nghiệm ban đầu
//     this->s_current = generateInitialSolution();
//     std::cout << this->s_current.toString();
//     archive.tryAdd(this->s_current);
//     logger->logProgress(0, 0, archive);
//     std::cout << "Initial solution generated. Starting main loop..." << std::endl;
//
//     int i_without_improvement = 0;
//     Solution s_best = this->s_current; // Track best solution tìm được
//
//     // 2. Vòng lặp ALNS chính
//     for (int i = 1; i <= config.maxIterations; ++i) {
//
//         // --- 2a. Tạo giải pháp mới ---
//         Solution s_new = this->s_current;
//
//         int destroyOpIdx = destroyPool.select(randomEngine);
//         int repairOpIdx = repairPool.select(randomEngine);
//
//         auto& destroyOp = *static_cast<IDestroyOperator*>(destroyPool.operators[destroyOpIdx].get());
//         auto& repairOp = *static_cast<IRepairOperator*>(repairPool.operators[repairOpIdx].get());
//
//         int nodesToRemove = calculateNodesToRemove();
//         if (nodesToRemove == 0) {
//             continue;
//         }
//
//         std::vector<int> unserved = destroyOp.execute(s_new, nodesToRemove, randomEngine);
//         repairOp.execute(s_new, unserved, randomEngine);
//
//
//         if (config.useLocalSearch && s_new.isFeasible()) {
//             localSearch.run(s_new);
//         }
//
//         // --- 2b. Đánh giá giải pháp ---
//         s_new.evaluateRoutes();
//
//         // ============================================================
//         // *** SỬA LỖI 1: Tách biệt logic Archive vs SA Acceptance ***
//         // ============================================================
//
//         double score = 0.0;
//         std::string archiveResultStr = "N/A";
//         bool has_improvement = false; // Flag xác định có cải thiện hay không
//
//         // BƯỚC 1: Xử lý giải pháp KHÔNG KHẢ THI
//         if (!s_new.isFeasible()) {
//             score = config.scoreIdentical;
//             archiveResultStr = "INFEASIBLE";
//             // Không tăng counter ở đây, sẽ xử lý thống nhất ở cuối
//         }
//         // BƯỚC 2: Xử lý giải pháp KHẢ THI
//         else {
//             // 2.1: Thử thêm vào Pareto Archive
//             AddResult archiveResult = archive.tryAdd(s_new);
//
//             switch (archiveResult) {
//                 case AddResult::DOMINATING:
//                     score = config.scoreDominating;
//                     archiveResultStr = "DOMINATING";
//                     has_improvement = true; // ✅ Đây là improvement rõ ràng
//                     break;
//                 case AddResult::NON_DOMINATED:
//                     score = config.scoreNonDominated;
//                     archiveResultStr = "NON_DOMINATED";
//                     has_improvement = true; // ✅ Đây cũng là improvement
//                     break;
//                 case AddResult::DOMINATED:
//                     score = config.scoreDominated;
//                     archiveResultStr = "DOMINATED";
//                     break;
//                 case AddResult::IDENTICAL:
//                     score = config.scoreIdentical;
//                     archiveResultStr = "IDENTICAL";
//                     break;
//             }
//
//             // ============================================================
//             // *** SỬA LỖI 2: SA Acceptance với Multi-Objective ***
//             // ============================================================
//
//             // 2.2: Simulated Annealing Acceptance
//             // Tính scalarized cost (weighted sum của các objectives)
//             double w_dist = 0.5, w_energy = 0.3, w_time = 0.2; // Tunable weights
//
//             double newCost = w_dist * s_new.getTotalDistance() +
//                            w_energy * s_new.getTotalEnergy() +
//                            w_time * s_new.getTotalTime();
//
//             double currentCost = w_dist * this->s_current.getTotalDistance() +
//                                w_energy * this->s_current.getTotalEnergy() +
//                                w_time * this->s_current.getTotalTime();
//
//             double delta = newCost - currentCost;
//
//             bool accepted_by_sa = false;
//
//             // Chấp nhận nếu tốt hơn
//             if (delta < -1e-6) { // Threshold nhỏ để tránh floating point error
//                 this->s_current = s_new;
//                 accepted_by_sa = true;
//                 has_improvement = true; // ✅ SA acceptance cũng là improvement
//             }
//             // Chấp nhận với xác suất (exploration)
//             else if (delta > 0 && currentTemperature > config.minTemperature) {
//                 double acceptProb = exp(-delta / currentTemperature);
//                 if (acceptProb > dist_0_1(randomEngine)) {
//                     this->s_current = s_new;
//                     accepted_by_sa = true;
//                     // Không set has_improvement = true ở đây vì đây là exploration
//                 }
//             }
//
//             // Track best solution (theo distance đơn giản, hoặc bạn có thể dùng dominance)
//             if (s_new.getTotalDistance() < s_best.getTotalDistance()) {
//                 s_best = s_new;
//             }
//         }
//
//         // ============================================================
//         // *** SỬA LỖI 3: Logic Counter Thống Nhất ***
//         // ============================================================
//
//         // Chỉ tăng counter khi KHÔNG CÓ improvement
//         if (has_improvement) {
//             i_without_improvement = 0; // Reset counter
//         } else {
//             i_without_improvement++; // Chỉ tăng ở MỘT CHỖ DUY NHẤT
//         }
//
//         // --- 2c. Cập nhật điểm operators ---
//         destroyPool.scores[destroyOpIdx] += score;
//         destroyPool.usages[destroyOpIdx]++;
//         repairPool.scores[repairOpIdx] += score;
//         repairPool.usages[repairOpIdx]++;
//
//         // --- 2d. Cập nhật nhiệt độ SA ---
//         currentTemperature *= config.coolingRate;
//         if (currentTemperature < config.minTemperature) {
//             currentTemperature = config.minTemperature;
//         }
//
//         // --- 2e. Logging theo segment ---
//         if (i % config.segmentIterations == 0) {
//             destroyPool.updateWeights(config.decayParameter);
//             repairPool.updateWeights(config.decayParameter);
//
//             auto now = std::chrono::high_resolution_clock::now();
//             long long time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();
//
//             logger->logOperatorSegment(i / config.segmentIterations, destroyPool, repairPool);
//             logger->logProgress(i, time_ms, archive);
//
//             std::cout << "Iter: " << std::setw(6) << i << "/" << config.maxIterations
//                       << " | Archive: " << std::setw(3) << archive.getSize()
//                       << " | NoImprove: " << std::setw(4) << i_without_improvement
//                       << " | Temp: " << std::fixed << std::setprecision(2) << currentTemperature
//                       << " | Result: " << std::setw(14) << archiveResultStr
//                       << " | Time: " << (double)time_ms / 1000.0 << "s" << std::endl;
//
//             destroyPool.resetScores();
//             repairPool.resetScores();
//         }
//
//         // --- 2f. Điều kiện dừng sớm ---
//         if (i_without_improvement >= config.maxIterationsWithoutImprovement) {
//             std::cout << "\n=== EARLY STOPPING ===" << std::endl;
//             std::cout << "No improvement in " << config.maxIterationsWithoutImprovement
//                       << " consecutive iterations." << std::endl;
//             std::cout << "Final iteration: " << i << "/" << config.maxIterations << std::endl;
//             break;
//         }
//
//     } // Kết thúc vòng lặp chính
//
//     // --- 3. Kết thúc ---
//     auto endTime = std::chrono::high_resolution_clock::now();
//     long long total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
//
//     std::cout << "\n=== ALNS FINISHED ===" << std::endl;
//     std::cout << "Total Time: " << (double)total_ms / 1000.0 << "s" << std::endl;
//     std::cout << "Final Pareto Front Size: " << archive.getSize() << std::endl;
//     std::cout << "Best Distance Found: " << s_best.getTotalDistance() << std::endl;
//
//     logger->logFinalFront(archive);
//     logger->logSummary(total_ms, config.maxIterations, archive.getSize());
//
//     return archive.getFront();
// }

// app/ALNSSolver.cpp

// (Copy toàn bộ hàm solve() này và dán thay thế hàm solve() cũ)

std::vector<Solution> ALNSSolver::solve() {
    if (destroyPool.operators.empty() || repairPool.operators.empty()) {
        throw std::runtime_error("ALNS Solver cannot run without at least one destroy and one repair operator.");
    }

    auto startTime = std::chrono::high_resolution_clock::now();
    std::cout << "ALNS Started..." << std::endl;

    // 1. Tạo nghiệm ban đầu
    this->s_current = generateInitialSolution();
    std::cout << this->s_current.toString();
    archive.tryAdd(this->s_current);
    logger->logProgress(0, 0, archive);
    std::cout << "Initial solution generated. Starting main loop..." << std::endl;

    int i_without_improvement = 0;
    Solution s_best = this->s_current;

    // 2. Vòng lặp ALNS chính
    for (int i = 1; i <= config.maxIterations; ++i) {

        // --- 2a. Tạo giải pháp mới ---
        Solution s_new = this->s_current;

        int destroyOpIdx = destroyPool.select(randomEngine);
        int repairOpIdx = repairPool.select(randomEngine);

        auto& destroyOp = *static_cast<IDestroyOperator*>(destroyPool.operators[destroyOpIdx].get());
        auto& repairOp = *static_cast<IRepairOperator*>(repairPool.operators[repairOpIdx].get());

        int nodesToRemove = calculateNodesToRemove();
        if (nodesToRemove == 0) {
            continue;
        }

        std::vector<int> unserved = destroyOp.execute(s_new, nodesToRemove, randomEngine);
        repairOp.execute(s_new, unserved, randomEngine);

        // *** ĐÃ XÓA LS KHỎI ĐÂY ***

        // --- 2b. Đánh giá giải pháp ---
        s_new.evaluateRoutes();

        // (Phần logic SỬA LỖI 1 giữ nguyên)
        double score = 0.0;
        std::string archiveResultStr = "N/A";
        bool has_improvement = false;

        if (!s_new.isFeasible()) {
            score = config.scoreIdentical;
            archiveResultStr = "INFEASIBLE";
        }
        else {
            // 2.1: Thử thêm vào Pareto Archive (Làm việc này TRƯỚC khi chạy SA)
            AddResult archiveResult = archive.tryAdd(s_new);

            switch (archiveResult) {
                case AddResult::DOMINATING:
                    score = config.scoreDominating;
                    archiveResultStr = "DOMINATING";
                    has_improvement = true;
                    break;
                case AddResult::NON_DOMINATED:
                    score = config.scoreNonDominated;
                    archiveResultStr = "NON_DOMINATED";
                    has_improvement = true;
                    break;
                case AddResult::DOMINATED:
                    score = config.scoreDominated;
                    archiveResultStr = "DOMINATED";
                    break;
                case AddResult::IDENTICAL:
                    score = config.scoreIdentical;
                    archiveResultStr = "IDENTICAL";
                    break;
            }

            // (Phần logic SỬA LỖI 2 giữ nguyên)
            // 2.2: Simulated Annealing Acceptance
            double w_vehicle = 0.65, w_dist = 0.25, w_energy = 0.05, w_time = 0.05; // Tunable weights

            double newCost = w_vehicle * s_new.getTotalVehicles() +
                            w_dist * s_new.getTotalDistance() +
                           w_energy * s_new.getTotalEnergy() +
                           w_time * s_new.getMaxTime();

            double currentCost = w_vehicle * s_current.getTotalVehicles() +
                                w_dist * this->s_current.getTotalDistance() +
                               w_energy * this->s_current.getTotalEnergy() +
                               w_time * this->s_current.getMaxTime();

            double delta = newCost - currentCost;

            // ============================================================
            // *** 💡 TỐI ƯU HÓA LOCAL SEARCH ***
            // ============================================================

            bool accepted_by_sa = false;

            // Chấp nhận nếu tốt hơn
            if (delta < -1e-6) {
                this->s_current = s_new;
                accepted_by_sa = true;
                has_improvement = true;

                // CHẠY LS TRÊN GIẢI PHÁP VỪA ĐƯỢC CHẤP NHẬN
            }
            // Chấp nhận với xác suất (exploration)
            else if (delta > 0 && currentTemperature > config.minTemperature) {
                double acceptProb = exp(-delta / currentTemperature);
                if (acceptProb > dist_0_1(randomEngine)) {
                    this->s_current = s_new;
                    accepted_by_sa = true;

                    // CHẠY LS TRÊN GIẢI PHÁP VỪA ĐƯỢC CHẤP NHẬN

                }
            }

            if (config.useLocalSearch && accepted_by_sa) {
                // Chạy LS với xác suất NGHỊCH ĐẢO
                std::uniform_int_distribution<int> dist_ls(1, 100);
                int lsProb = 100 / config.localSearchIntensity; // Intensity càng cao, prob càng cao

                if (dist_ls(randomEngine) <= lsProb) {
                    localSearch.run(this->s_current);
                    this->s_current.evaluateRoutes();
                    archive.tryAdd(this->s_current);
                }
            }

            // Track best solution (giữ nguyên)
            if (s_new.getTotalDistance() < s_best.getTotalDistance()) {
                s_best = s_new;
            }
        } // Kết thúc else (feasible solution)

        // (Phần logic SỬA LỖI 3 giữ nguyên)
        if (has_improvement) {
            i_without_improvement = 0;
        } else {
            i_without_improvement++;
        }

        // --- 2c. Cập nhật điểm operators ---
        destroyPool.scores[destroyOpIdx] += score;
        destroyPool.usages[destroyOpIdx]++;
        repairPool.scores[repairOpIdx] += score;
        repairPool.usages[repairOpIdx]++;

        // --- 2d. Cập nhật nhiệt độ SA ---
        currentTemperature *= config.coolingRate;
        if (currentTemperature < config.minTemperature) {
            currentTemperature = config.minTemperature;
        }

        // --- 2e. Logging theo segment ---
        if (i % config.segmentIterations == 0) {
            destroyPool.updateWeights(config.decayParameter);
            repairPool.updateWeights(config.decayParameter);

            auto now = std::chrono::high_resolution_clock::now();
            long long time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();

            logger->logOperatorSegment(i / config.segmentIterations, destroyPool, repairPool);
            logger->logProgress(i, time_ms, archive);

            std::cout << "Iter: " << std::setw(6) << i << "/" << config.maxIterations
                      << " | Archive: " << std::setw(3) << archive.getSize()
                      << " | NoImprove: " << std::setw(4) << i_without_improvement
                      << " | Temp: " << std::fixed << std::setprecision(2) << currentTemperature
                      << " | Result: " << std::setw(14) << archiveResultStr
                      << " | Time: " << (double)time_ms / 1000.0 << "s" << std::endl;

            destroyPool.resetScores();
            repairPool.resetScores();
        }

        // --- 2f. Điều kiện dừng sớm ---
        if (i_without_improvement >= config.maxIterationsWithoutImprovement) {
            std::cout << "\n=== EARLY STOPPING ===" << std::endl;
            std::cout << "No improvement in " << config.maxIterationsWithoutImprovement
                      << " consecutive iterations." << std::endl;
            std::cout << "Final iteration: " << i << "/" << config.maxIterations << std::endl;
            break;
        }

    } // Kết thúc vòng lặp chính

    // --- 3. Kết thúc ---
    auto endTime = std::chrono::high_resolution_clock::now();
    long long total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

    std::cout << "\n=== ALNS FINISHED ===" << std::endl;
    std::cout << "Total Time: " << (double)total_ms / 1000.0 << "s" << std::endl;
    std::cout << "Final Pareto Front Size: " << archive.getSize() << std::endl;
    std::cout << "Best Distance Found: " << s_best.getTotalDistance() << std::endl;

    logger->logFinalFront(archive);
    logger->logSummary(total_ms, config.maxIterations, archive.getSize());

    return archive.getFront();
}

int ALNSSolver::calculateNodesToRemove() {
    // (SỬA LỖI 'vector too long')

    // Nếu không có khách hàng, không xóa gì cả
    if (totalCustomers == 0) {
        return 0;
    }

    // Nếu config không cho phép xóa, không xóa gì cả
    if (config.maxRemoval <= 0.0) {
        return 0;
    }

    // 1. Tính toán số lượng min/max
    int min_num = static_cast<int>(totalCustomers * config.minRemoval);
    int max_num = static_cast<int>(totalCustomers * config.maxRemoval);

    // 2. Đảm bảo ít nhất là 1 (nếu có thể)
    min_num = std::max(1, min_num);
    max_num = std::max(min_num, max_num); // Đảm bảo max >= min

    // 3. Đảm bảo không vượt quá tổng số khách hàng
    max_num = std::min(max_num, totalCustomers);
    min_num = std::min(min_num, max_num); // Đảm bảo min <= max

    // 4. Dùng uniform_int_distribution để chọn
    std::uniform_int_distribution<int> dist(min_num, max_num);
    return dist(randomEngine);
}


// ******************************************************************
// ** 4. TRIỂN KHAI CÁC HÀM PRIVATE
// ** (Phần này của bạn đã đúng, giữ nguyên)
// ******************************************************************

// Solution ALNSSolver::generateInitialSolution() {
//     Solution initialSol(instance);
//
//     std::vector<int> allCustomers;
//     for (const auto& node : instance->getNodes()) {
//         if (dynamic_cast<Customer*>(node.get())) {
//             allCustomers.push_back(node->getId());
//         }
//     }
//
//     if (allCustomers.empty()) {
//         std::cerr << "Warning: No customers found in instance." << std::endl;
//         return initialSol;
//     }
//
//     if (repairPool.operators.empty()) {
//         throw std::runtime_error("Cannot generate initial solution: No repair operators registered.");
//     }
//
//     auto& initialRepairOp = *static_cast<IRepairOperator*>(repairPool.operators[1].get());
//     initialRepairOp.execute(initialSol, allCustomers, rng);
//
//     initialSol.evaluateRoutes();
//     return initialSol;
// }

Solution ALNSSolver::generateInitialSolution() {
    std::cout << "Generate Initial Solution using Smart Nearest Neighbor with Charging..." << std::endl;
    Solution sol(instance);

    // Lấy danh sách khách hàng chưa phục vụ
    std::vector<int> unserved;
    for (const auto& node : instance->getNodes()) {
        if (dynamic_cast<Customer*>(node.get()) != nullptr && node->getId() != 0) {
            unserved.push_back(node->getId());
        }
    }

    // Lấy danh sách trạm sạc
    std::vector<int> stationIds;
    for (const auto& node : instance->getNodes()) {
        if (dynamic_cast<Station*>(node.get()) != nullptr) {
            stationIds.push_back(node->getId());
        }
    }

    const int depotId = 0;
    auto depotNode = instance->getNodeById(depotId);
    const double maxRouteTime = depotNode->getDueDate();
    const double vehCapacity = instance->getVehicleCapacity();
    const double vehMaxBattery = instance->getVehicleBattery();
    const double vehEnergyRate = instance->getVehicleEnergyRate();

    std::cout << "Total customers: " << unserved.size() << "\n";
    std::cout << "Stations: " << stationIds.size() << "\n";
    std::cout << "Vehicle: capacity=" << vehCapacity << ", battery=" << vehMaxBattery << "\n";
    std::cout << "Max route time: " << maxRouteTime << "\n\n";

    int maxAttempts = unserved.size() * 3;
    int attemptCount = 0;
    int consecutiveFailures = 0;

    // Lambda: Tính chi phí thực tế để phục vụ khách hàng (bao gồm cả sạc nếu cần)
    auto calculateRealCost = [&](int fromNode, int custId, double currentTime, double currentBatt) -> double {
        auto custNode = std::dynamic_pointer_cast<Customer>(instance->getNodeById(custId));

        double directDist = instance->getDistance(fromNode, custId);
        double directEnergy = directDist * vehEnergyRate;

        // Nếu đủ pin đi trực tiếp
        if (directEnergy <= currentBatt) {
            return directDist;
        }

        // Cần sạc -> tìm trạm tốt nhất trên đường
        double bestCost = std::numeric_limits<double>::max();
        for (int sid : stationIds) {
            double dist_to_station = instance->getDistance(fromNode, sid);
            double energy_to_station = dist_to_station * vehEnergyRate;

            if (energy_to_station <= currentBatt) {
                double dist_station_to_cust = instance->getDistance(sid, custId);
                double totalDist = dist_to_station + dist_station_to_cust;

                if (totalDist < bestCost) {
                    bestCost = totalDist;
                }
            }
        }

        return bestCost;
    };

    // Lambda: Đánh giá mức độ "urgent" của khách hàng (time window chặt)
    auto getUrgency = [&](int custId, double currentTime) -> double {
        auto custNode = std::dynamic_pointer_cast<Customer>(instance->getNodeById(custId));
        double timeToDeadline = custNode->getDueDate() - currentTime;
        double windowSize = custNode->getDueDate() - custNode->getReadyTime();

        // Urgent nếu sắp hết time window
        return 1.0 / (timeToDeadline + 1.0);
    };

    while (!unserved.empty() && attemptCount < maxAttempts) {
        attemptCount++;

        auto vehicle = std::make_shared<Vehicle>(
            static_cast<int>(sol.getNumRoutes()),
            vehCapacity, vehMaxBattery, vehEnergyRate);
        Route route(static_cast<int>(sol.getNumRoutes()), vehicle, instance);

        int current = depotId;
        double currentTime = 0.0;
        double remainingCap = vehCapacity;
        double remainingBatt = vehMaxBattery;

        std::vector<int> currentRouteNodes;
        bool addedAny = false;
        int stepsWithoutCustomer = 0;
        const int maxStepsWithoutCustomer = 2; // Giới hạn số lần sạc liên tiếp

        while (true) {
            int bestCustomer = -1;
            double bestScore = std::numeric_limits<double>::max();

            // === CHIẾN LƯỢC 1: TÌM KHÁCH HÀNG VỚI SCORING FUNCTION ===
            for (int cid : unserved) {
                auto custNode = std::dynamic_pointer_cast<Customer>(instance->getNodeById(cid));

                double demand = custNode->getDemand();
                double dist = instance->getDistance(current, cid);
                double travelTime = instance->getTime(current, cid);
                double energyToCustomer = dist * vehEnergyRate;
                double arrivalTime = currentTime + travelTime;

                // Kiểm tra ràng buộc cơ bản
                if (demand > remainingCap) continue;
                if (energyToCustomer > remainingBatt) continue;
                if (arrivalTime > custNode->getDueDate()) continue;

                // Kiểm tra khả năng hoàn thành route
                double waitTime = std::max(0.0, custNode->getReadyTime() - arrivalTime);
                double timeAtCustomer = arrivalTime + waitTime + custNode->getServiceTime();
                double energyAtCustomer = remainingBatt - energyToCustomer;

                double timeToDepot = instance->getTime(cid, depotId);
                double energyToDepot = instance->getDistance(cid, depotId) * vehEnergyRate;

                bool canReturnDirectly = (energyToDepot <= energyAtCustomer) &&
                                       (timeAtCustomer + timeToDepot <= maxRouteTime);

                bool canReturnViaStation = false;
                if (!canReturnDirectly && !stationIds.empty()) {
                    for (int sid : stationIds) {
                        double dist_c_s = instance->getDistance(cid, sid);
                        double energy_c_s = dist_c_s * vehEnergyRate;

                        if (energy_c_s <= energyAtCustomer) {
                            auto stationNode = std::dynamic_pointer_cast<Station>(instance->getNodeById(sid));
                            double time_c_s = instance->getTime(cid, sid);
                            double arrival_s = timeAtCustomer + time_c_s;

                            double energyAfterTravel_s = energyAtCustomer - energy_c_s;
                            double chargeNeeded = vehMaxBattery - energyAfterTravel_s;
                            double chargeTime = chargeNeeded * stationNode->getChargingRate();

                            double timeAtStation = arrival_s + chargeTime;
                            double time_s_d = instance->getTime(sid, depotId);

                            if (timeAtStation + time_s_d <= maxRouteTime) {
                                canReturnViaStation = true;
                                break;
                            }
                        }
                    }
                }

                if (!canReturnDirectly && !canReturnViaStation) continue;

                // === SCORING FUNCTION: Kết hợp nhiều yếu tố ===
                double realCost = calculateRealCost(current, cid, currentTime, remainingBatt);
                double urgency = getUrgency(cid, currentTime);
                double capacityUtilization = demand / vehCapacity;

                // Score thấp = tốt hơn
                // - Ưu tiên khách gần (realCost thấp)
                // - Ưu tiên khách urgent (urgency cao)
                // - Ưu tiên khách có demand lớn (tận dụng capacity)
                double score = realCost * 1.0 - urgency * 50.0 - capacityUtilization * 20.0;

                if (score < bestScore) {
                    bestScore = score;
                    bestCustomer = cid;
                }
            }

            // === XỬ LÝ KHÁCH HÀNG TỐT NHẤT ===
            if (bestCustomer != -1) {
                auto custNode = std::dynamic_pointer_cast<Customer>(instance->getNodeById(bestCustomer));

                double dist = instance->getDistance(current, bestCustomer);
                double travelTime = instance->getTime(current, bestCustomer);
                double arrivalTime = currentTime + travelTime;
                double waitTime = std::max(0.0, custNode->getReadyTime() - arrivalTime);
                double serviceTime = custNode->getServiceTime();
                double energyUsed = dist * vehEnergyRate;
                double demand = custNode->getDemand();

                current = bestCustomer;
                currentTime = arrivalTime + waitTime + serviceTime;
                remainingBatt -= energyUsed;
                remainingCap -= demand;
                currentRouteNodes.push_back(bestCustomer);
                unserved.erase(std::remove(unserved.begin(), unserved.end(), bestCustomer), unserved.end());
                addedAny = true;
                stepsWithoutCustomer = 0;
                attemptCount = 0;
                consecutiveFailures = 0;

                continue;
            }

            // === CHIẾN LƯỢC 2: SMART CHARGING - Chỉ sạc khi thực sự cần thiết ===
            stepsWithoutCustomer++;

            if (stepsWithoutCustomer > maxStepsWithoutCustomer) {
                // Đã sạc quá nhiều mà vẫn không phục vụ được -> kết thúc route
                break;
            }

            // Kiểm tra xem có khách hàng nào CÓ THỂ đến được sau khi sạc không
            bool worthCharging = false;
            for (int cid : unserved) {
                auto custNode = std::dynamic_pointer_cast<Customer>(instance->getNodeById(cid));
                double demand = custNode->getDemand();

                if (demand <= remainingCap) {
                    // Tính xem nếu sạc đầy có đến được khách này không
                    double distToCust = instance->getDistance(current, cid);
                    double energyToCust = distToCust * vehEnergyRate;

                    if (energyToCust <= vehMaxBattery) {
                        worthCharging = true;
                        break;
                    }
                }
            }

            if (!worthCharging) {
                // Không đáng sạc -> kết thúc route
                break;
            }

            // Tìm trạm sạc TỐT NHẤT (không chỉ gần nhất)
            int bestStation = -1;
            double bestStationScore = std::numeric_limits<double>::max();

            for (int sid : stationIds) {
                double dist_c_s = instance->getDistance(current, sid);
                double energy_c_s = dist_c_s * vehEnergyRate;

                if (energy_c_s > remainingBatt) continue;

                auto stationNode = std::dynamic_pointer_cast<Station>(instance->getNodeById(sid));
                double time_c_s = instance->getTime(current, sid);
                double arrival_s = currentTime + time_c_s;

                double energyAfterTravel_s = remainingBatt - energy_c_s;
                double chargeNeeded = vehMaxBattery - energyAfterTravel_s;
                if (chargeNeeded < 1e-6) continue; // Bỏ qua trạm không cần sạc

                double chargeTime = chargeNeeded * stationNode->getChargingRate();
                double timeAtStation = arrival_s + chargeTime;
                double time_s_d = instance->getTime(sid, depotId);

                if (timeAtStation + time_s_d > maxRouteTime) continue;

                // Đánh giá trạm dựa trên: khoảng cách + số khách hàng có thể đến được từ trạm
                int reachableCustomers = 0;
                for (int cid : unserved) {
                    double distFromStation = instance->getDistance(sid, cid);
                    double energyFromStation = distFromStation * vehEnergyRate;
                    if (energyFromStation <= vehMaxBattery) {
                        reachableCustomers++;
                    }
                }

                // Score thấp = tốt (gần + nhiều khách tiếp cận được)
                double score = dist_c_s * 1.0 - reachableCustomers * 10.0;

                if (score < bestStationScore) {
                    bestStationScore = score;
                    bestStation = sid;
                }
            }

            if (bestStation != -1) {
                auto stationNode = std::dynamic_pointer_cast<Station>(instance->getNodeById(bestStation));
                double dist = instance->getDistance(current, bestStation);
                double travelTime = instance->getTime(current, bestStation);
                double arrivalTime = currentTime + travelTime;
                double energyUsed = dist * vehEnergyRate;

                double energyAfterTravel = remainingBatt - energyUsed;
                double chargeNeeded = vehMaxBattery - energyAfterTravel;
                double chargeTime = chargeNeeded * stationNode->getChargingRate();

                current = bestStation;
                currentTime = arrivalTime + chargeTime;
                remainingBatt = vehMaxBattery;
                currentRouteNodes.push_back(bestStation);

                continue;
            }

            // Không tìm được gì -> kết thúc route
            break;
        }

        // === HOÀN TẤT ROUTE ===
        if (!addedAny && !unserved.empty()) {
            consecutiveFailures++;

            if (consecutiveFailures >= 3) {
                std::cerr << "ERROR: Cannot serve remaining customers after multiple attempts.\n";
                std::cerr << "Remaining: " << unserved.size() << " customers\n";

                // In thông tin chi tiết
                for (int id : unserved) {
                    auto custNode = std::dynamic_pointer_cast<Customer>(instance->getNodeById(id));
                    double minDist = std::numeric_limits<double>::max();
                    for (int sid : stationIds) {
                        double d = instance->getDistance(sid, id);
                        minDist = std::min(minDist, d);
                    }
                    double depotDist = instance->getDistance(depotId, id);

                    std::cerr << "  Customer " << id << ": demand=" << custNode->getDemand()
                              << ", depot_dist=" << depotDist
                              << ", nearest_station_dist=" << minDist
                              << ", TW=[" << custNode->getReadyTime() << "," << custNode->getDueDate() << "]\n";
                }
                break;
            }
            continue;
        }

        if (addedAny) {
            for (size_t i = 0; i < currentRouteNodes.size(); ++i) {
                route.addNode(currentRouteNodes[i], i + 1);
            }

            route.evaluate();

            if (route.isFeasible()) {
                sol.addRoute(route);
                consecutiveFailures = 0;
            } else {
                for (int nodeId : currentRouteNodes) {
                    if (dynamic_cast<Customer*>(instance->getNodeById(nodeId).get()) != nullptr) {
                        unserved.push_back(nodeId);
                    }
                }
                consecutiveFailures++;
            }
        }
    }

    if (!unserved.empty()) {
        std::cerr << "\n⚠ WARNING: " << unserved.size() << " customers could not be served!\n";
    }

    sol.evaluateRoutes();

    std::cout << "\n========================================\n";
    std::cout << "Initial Solution Summary:\n";
    std::cout << "  Routes: " << sol.getNumRoutes() << "\n";
    std::cout << "  Unserved: " << unserved.size() << "\n";
    std::cout << "  Total Distance: " << sol.getTotalDistance() << "\n";
    std::cout << "  Feasible: " << (sol.isFeasible() ? "YES" : "NO") << "\n";
    std::cout << "========================================\n";

    return sol;
}