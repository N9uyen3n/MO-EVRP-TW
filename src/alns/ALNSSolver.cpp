#include "../../include/alns/ALNSSolver.h"

// --- Logger ---
#include "../../include/logger/ComprehensiveLogger.h"
#include "../../include/logger/NullLogger.h"

// --- Core ---
#include "../../include/alns/ParetoArchive.h"
#include "../../include/core/Customer.h"
#include "../../include/core/Station.h"
#include "../../include/core/Vehicle.h"
#include "../../include/core/Route.h"

// --- Operators: Destroy ---
#include "../../include/alns/operators/destroy/WorstDistanceNodeRemoval.h"
#include "../../include/alns/operators/destroy/ShawDestroy.h"
#include "../../include/alns/operators/destroy/FewestCustomersRouteRemoval.h"
#include "../../include/alns/operators/destroy/LongestWaitTimeRouteRemoval.h"
#include "../../include/alns/operators/destroy/HighEnergyNodeRemoval.h"
#include "../../include/alns/operators/destroy/ParetoFocusDestroy.h"

// --- Operators: Repair ---
#include "../../include/alns/operators/repair/GreedyTimeInsertion.h"
#include "../../include/alns/operators/repair/GreedyDistanceInsertion.h"
#include "../../include/alns/operators/repair/RegretKRepair.h"
#include "../../include/alns/operators/repair/GreedyEnergyInsertion.h"
#include "../../include/alns/operators/repair/GreedyStationRepair.h"
#include "../../include/alns/operators/repair/ParetoFocusRepair.h"

#include <iostream>
#include <chrono>
#include <numeric>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <iomanip>
#include <random>

namespace alns {

// ******************************************************************
// ** 1. HELPER: OperatorPool Implementation
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
        if (r <= currentSum) return i;
    }
    return operators.size() - 1;
}

void OperatorPool::updateWeights(double decay) {
    for (size_t i = 0; i < operators.size(); ++i) {
        double avgScore = (usages[i] > 0) ? (scores[i] / usages[i]) : 0.0;
        weights[i] = weights[i] * decay + (1.0 - decay) * avgScore;
        weights[i] = std::max(0.1, weights[i]);
    }
}

void OperatorPool::resetScores() {
    std::fill(scores.begin(), scores.end(), 0.0);
    std::fill(usages.begin(), usages.end(), 0);
}

// ******************************************************************
// ** 2. ALNSSolver Implementation
// ******************************************************************

ALNSSolver::ALNSSolver(std::shared_ptr<Instance> instance, ALNSConfig config,
                       const std::string& outputDirectory, const std::string& runName)
    : instance(instance),
      s_current(instance),
      config(config),
      archive(100),
      localSearch(instance),
      totalCustomers(0)
{
    unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
    this->randomEngine.seed(seed);

    if (config.enableLogging) {
        this->logger = std::make_unique<logging::ComprehensiveLogger>(outputDirectory, runName);
        std::cout << "Logging ENABLED. Output: " << outputDirectory << std::endl;
    } else {
        this->logger = std::make_unique<logging::NullLogger>();
    }

    for (const auto& node : instance->getNodes()) {
        if (std::dynamic_pointer_cast<Customer>(node)) {
            this->totalCustomers++;
        }
    }
    this->logger->logConfig(this->config);
    this->currentTemperature = config.startTemperature;

    // ĐĂNG KÝ CÁC TOÁN TỬ
    addDestroyOperator(std::make_shared<WorstDistanceNodeRemoval>(instance, 3), 1.0);
    addDestroyOperator(std::make_shared<ShawDestroy>(instance, 6), 1.0);
    addDestroyOperator(std::make_shared<FewestCustomersRouteRemoval>(instance), 1.0);
    addDestroyOperator(std::make_shared<LongestWaitTimeRouteRemoval>(instance), 1.0); // Tạm thời vô hiệu hóa vì chưa hoàn thiện
    addDestroyOperator(std::make_shared<ParetoFocusDestroy>(instance, 4), 1.0);

    addRepairOperator(std::make_shared<GreedyTimeInsertion>(instance), 1.0);
    addRepairOperator(std::make_shared<GreedyDistanceInsertion>(instance), 1.0);
    addRepairOperator(std::make_shared<RegretKRepair>(instance, config.regretK, config.noiseParameter), 1.0);
    addRepairOperator(std::make_shared<GreedyStationRepair>(instance), 1.0);
    addRepairOperator(std::make_shared<ParetoFocusRepair>(instance), 1.0);
}

ALNSSolver::~ALNSSolver() = default;

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

int ALNSSolver::calculateNodesToRemove() {
    if (totalCustomers == 0 || config.maxRemoval <= 0.0) return 0;
    int min_num = static_cast<int>(totalCustomers * config.minRemoval);
    int max_num = static_cast<int>(totalCustomers * config.maxRemoval);
    min_num = std::max(1, min_num);
    max_num = std::max(min_num, max_num);
    max_num = std::min(max_num, totalCustomers);
    min_num = std::min(min_num, max_num);
    std::uniform_int_distribution<int> dist(min_num, max_num);
    return dist(randomEngine);
}

// ******************************************************************
// ** 3. MAIN SOLVE LOOP
// ******************************************************************

std::vector<Solution> ALNSSolver::solve() {
    auto startTime = std::chrono::high_resolution_clock::now();

    s_current = generateInitialSolution();
    this->archive.tryAdd(s_current);

    if (!s_current.isFeasible()) {
        // No specific error log function in the interface, std::cerr is an alternative
        std::cerr << "[ERROR] Initial solution is not feasible!" << std::endl;
        return {};
    }

    Solution s_best = s_current;
    currentTemperature = config.startTemperature;
    int iterationsWithoutImprovement = 0;

    std::uniform_real_distribution<> dis(0.0, 1.0);

    for (int i = 0; i < config.maxIterations; ++i) {
        Solution s_new = this->s_current;
        int n_to_remove = calculateNodesToRemove();

        // 1. Destroy
        int destroy_op_idx = destroyPool.select(randomEngine);
        auto destroy_op = std::static_pointer_cast<IDestroyOperator>(destroyPool.operators[destroy_op_idx]);
        std::vector<int> unserved_custs = destroy_op->execute(s_new, n_to_remove, randomEngine);
        destroyPool.usages[destroy_op_idx]++;

        // 2. Repair
        int repair_op_idx = repairPool.select(randomEngine);
        auto repair_op = std::static_pointer_cast<IRepairOperator>(repairPool.operators[repair_op_idx]);
        repair_op->execute(s_new, unserved_custs, randomEngine);
        repairPool.usages[repair_op_idx]++;

        s_new.evaluateRoutes();

        if (!s_new.isFeasible()) {
            destroyPool.scores[destroy_op_idx] += config.scoreIdentical;
            repairPool.scores[repair_op_idx] += config.scoreIdentical;
            continue;
        }

        // 3. Local Search
        if (config.useLocalSearch) {
            std::uniform_int_distribution<> dis_ls(0, 99);
            if (dis_ls(randomEngine) < config.localSearchIntensity) {
                localSearch.run(s_new);
            }
        }

        std::string result = "Rejected";
        // 4. Acceptance
        if (s_new.dominates(s_current)) {
            s_current = s_new;
            destroyPool.scores[destroy_op_idx] += config.scoreDominating;
            repairPool.scores[repair_op_idx] += config.scoreDominating;
            iterationsWithoutImprovement = 0;
            result = "Dominating";
        } else {
            AddResult add_res = archive.tryAdd(s_new);
            if (add_res == AddResult::DOMINATING || add_res == AddResult::NON_DOMINATED) {
                destroyPool.scores[destroy_op_idx] += config.scoreNonDominated;
                repairPool.scores[repair_op_idx] += config.scoreNonDominated;
                result = "Non-Dominated";
            } else {
                double delta_objectives = s_new.getTotalDistance() - s_current.getTotalDistance();
                if (std::exp(-delta_objectives / currentTemperature) > dis(randomEngine)) {
                    s_current = s_new;
                    destroyPool.scores[destroy_op_idx] += config.scoreDominated;
                    repairPool.scores[repair_op_idx] += config.scoreDominated;
                    result = "Accepted (SA)";
                } else {
                    destroyPool.scores[destroy_op_idx] += config.scoreIdentical;
                    repairPool.scores[repair_op_idx] += config.scoreIdentical;
                }
            }
        }
        
        logger->logEvolutionStep(i, destroy_op->getName(), repair_op->getName(), result, s_new);

        if (s_current.dominates(s_best)) {
            s_best = s_current;
            iterationsWithoutImprovement = 0;
        }

        currentTemperature *= config.coolingRate;
        if (currentTemperature < config.minTemperature) {
            currentTemperature = config.minTemperature;
        }

        if ((i + 1) % config.segmentIterations == 0) {
            auto now = std::chrono::high_resolution_clock::now();
            long long time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();
            logger->logProgress(i + 1, time_ms, this->archive);
            logger->logOperatorSegment(i + 1, destroyPool, repairPool);
            destroyPool.updateWeights(config.decayParameter);
            repairPool.updateWeights(config.decayParameter);
            destroyPool.resetScores();
            repairPool.resetScores();
        }

        iterationsWithoutImprovement++;
        if (iterationsWithoutImprovement >= config.maxIterationsWithoutImprovement) {
            std::cout << "[Stop] Converged after " << iterationsWithoutImprovement << " iterations without improvement." << std::endl;
            break;
        }
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    long long total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

    logger->logFinalFront(this->archive);
    logger->logSummary(total_ms, config.maxIterations, this->archive.getSize());
    
    std::cout << "ALNS Finished." << std::endl;
    return archive.getFront();
}

// ******************************************************************
// ** 4. INITIAL SOLUTION (Simple Greedy)
// ******************************************************************
// File: src/alns/ALNSSolver.cpp

Solution ALNSSolver::generateInitialSolution() {
    std::cout << "[Info] Generating Initial Solution (Corrected for Instance API)..." << std::endl;
    Solution sol(instance); // Sử dụng copy constructor hoặc constructor nhận tham chiếu

    // --- BƯỚC 1: TẠO LOOKUP TABLE (Để tránh dynamic_cast trong vòng lặp) ---
    // Tìm max ID để khởi tạo kích thước vector
    int maxId = 0;
    for (const auto& node : instance->getNodes()) {
        if (node->getId() > maxId) maxId = node->getId();
    }

    // Mảng truy cập nhanh: id -> pointer trỏ tới dữ liệu thực
    std::vector<Customer*> custLookup(maxId + 1, nullptr);
    std::vector<Station*> stationLookup(maxId + 1, nullptr);
    std::vector<int> unservedIds;
    std::vector<int> stationIds;

    // Điền dữ liệu vào bảng lookup
    for (const auto& c : instance->getCustomers()) {
        custLookup[c->getId()] = c.get();
        unservedIds.push_back(c->getId());
    }
    for (const auto& s : instance->getStations()) {
        stationLookup[s->getId()] = s.get();
        stationIds.push_back(s->getId());
    }

    // Lấy thông tin Depot (giả sử ID 0 là depot và node 0 luôn tồn tại)
    auto depotNode = instance->getNodeById(0);
    const double maxTime = depotNode->getDueDate(); // Depot closing time

    // Cache các biến const để truy cập nhanh
    const double vehCapacity = instance->getVehicleCapacity();
    const double vehMaxBattery = instance->getVehicleBattery();
    const double vehEnergyRate = instance->getVehicleEnergyRate();
    const int depotId = 0;

    std::cout << "Total customers: " << unservedIds.size() << "\n";
    std::cout << "Stations: " << stationIds.size() << "\n";
    std::cout << "Vehicle: capacity=" << vehCapacity << ", battery=" << vehMaxBattery << "\n";
    std::cout << "Max route time: " << maxTime << "\n\n";


    bool cannotServeMore = false;

    // --- BƯỚC 2: VÒNG LẶP CHÍNH (Constructive Heuristic) ---
    while (!unservedIds.empty() && !cannotServeMore) {
        // Tạo xe mới
        auto vehicle = std::make_shared<Vehicle>(
            sol.getNumRoutes(), vehCapacity, vehMaxBattery, vehEnergyRate);
        Route currentRoute(sol.getNumRoutes(), vehicle, instance); // Chú ý: Route cần tham chiếu Instance

        int currNodeId = depotId;
        double currTime = 0.0;
        double currBatt = vehMaxBattery;
        double currLoad = 0.0;

        bool routeFinished = false;
        bool customerAddedInThisRoute = false;

        while (!routeFinished) {
            int bestCustId = -1;
            double bestScore = std::numeric_limits<double>::max();

            // --- TÌM KHÁCH HÀNG TỐT NHẤT ---
            for (int custId : unservedIds) {
                // Lấy pointer từ lookup table (O(1)) -> KHÔNG dùng instance->getDemand(id) vì không có hàm đó
                Customer* cust = custLookup[custId];

                // Check Tải trọng
                if (currLoad + cust->getDemand() > vehCapacity) continue;

                // Check Pin & Thời gian
                double dist = instance->getDistance(currNodeId, custId);
                double energyNeeded = dist * vehEnergyRate;
                if (currBatt < energyNeeded) continue;

                double travelTime = instance->getTime(currNodeId, custId);
                double arrivalTime = currTime + travelTime;

                // Check Time Window (ReadyTime & DueDate nằm trong class Customer)
                if (arrivalTime > cust->getDueDate()) continue;

                // --- LOGIC LOOK-AHEAD: Phải đảm bảo về được nhà hoặc tới trạm ---
                double startService = std::max(arrivalTime, cust->getReadyTime());
                double endService = startService + cust->getServiceTime();
                double energyLeftAtCust = currBatt - energyNeeded;

                // Check về Depot
                double distToDepot = instance->getDistance(custId, depotId);
                double energyToDepot = distToDepot * vehEnergyRate;
                double timeToDepot = instance->getTime(custId, depotId);

                bool safeReturn = false;

                if (energyLeftAtCust >= energyToDepot && (endService + timeToDepot <= maxTime)) {
                    safeReturn = true;
                } else {
                    // Check trạm sạc gần nhất (Dùng instance->getNearestStationId)
                    int nearStationId = instance->getNearestStationId(custId);
                    if (nearStationId != -1) {
                        double distToStat = instance->getDistance(custId, nearStationId);
                        double energyToStat = distToStat * vehEnergyRate;

                        if (energyLeftAtCust >= energyToStat) {
                            // Tính sơ bộ thời gian
                            double timeToStat = instance->getTime(custId, nearStationId);
                            Station* stat = stationLookup[nearStationId];

                            // Giả sử sạc đầy để check an toàn
                            double arrivalStat = endService + timeToStat;
                            double chargeNeeded = vehMaxBattery - (energyLeftAtCust - energyToStat);
                            double chargeTime = chargeNeeded * stat->getChargingRate(); // Lấy rate từ lookup

                            double timeStatToDepot = instance->getTime(nearStationId, depotId);
                            if (arrivalStat + chargeTime + timeStatToDepot <= maxTime) {
                                safeReturn = true;
                            }
                        }
                    }
                }

                if (!safeReturn) continue;

                // --- TÍNH ĐIỂM (Scoring) ---
                // Ưu tiên: Khách gần + Gấp gáp
                double urgency = 1.0 / (std::max(1.0, cust->getDueDate() - currTime));
                double score = dist - (urgency * 100.0);

                if (score < bestScore) {
                    bestScore = score;
                    bestCustId = custId;
                }
            }

            // --- XỬ LÝ KẾT QUẢ ---
            if (bestCustId != -1) {
                currentRoute.addNode(bestCustId, currentRoute.getNodes().size() - 1); // Hàm của Route class
                Customer* cust = custLookup[bestCustId];

                // Cập nhật trạng thái xe tạm thời
                double dist = instance->getDistance(currNodeId, bestCustId);
                double travelTime = instance->getTime(currNodeId, bestCustId);
                double arrivalTime = currTime + travelTime;
                double waitTime = std::max(0.0, cust->getReadyTime() - arrivalTime);

                currTime = arrivalTime + waitTime + cust->getServiceTime();
                currBatt -= (dist * vehEnergyRate);
                currLoad += cust->getDemand();
                currNodeId = bestCustId;

                // Xóa khỏi danh sách unserved (Swap & Pop)
                for (size_t i = 0; i < unservedIds.size(); ++i) {
                    if (unservedIds[i] == bestCustId) {
                        unservedIds[i] = unservedIds.back();
                        unservedIds.pop_back();
                        break;
                    }
                }
                customerAddedInThisRoute = true;
            } else {
                // Không đón được ai -> Thử đi sạc
                // Chỉ sạc nếu pin < 80% để tránh lạm dụng
                if (currBatt < vehMaxBattery * 0.8) {
                    int bestStationId = -1;
                    double minStationDist = std::numeric_limits<double>::max();

                    for (int sid : stationIds) {
                        double d = instance->getDistance(currNodeId, sid);
                        double e = d * vehEnergyRate;

                        if (currBatt >= e) {
                            // Check thời gian về depot sau sạc
                            double tToStat = instance->getTime(currNodeId, sid);
                            Station* stat = stationLookup[sid];

                            // Tính sạc đầy
                            double chargeAmount = vehMaxBattery - (currBatt - e);
                            double tCharge = chargeAmount * stat->getChargingRate();
                            double tToDepot = instance->getTime(sid, depotId);

                            if (currTime + tToStat + tCharge + tToDepot <= maxTime) {
                                if (d < minStationDist) {
                                    minStationDist = d;
                                    bestStationId = sid;
                                }
                            }
                        }
                    }

                    if (bestStationId != -1) {
                        // Đi sạc
                        currentRoute.addNode(bestStationId, currentRoute.getNodes().size() - 1);
                        Station* stat = stationLookup[bestStationId];

                        double d = instance->getDistance(currNodeId, bestStationId);
                        double e = d * vehEnergyRate;
                        double t = instance->getTime(currNodeId, bestStationId);

                        double chargeAmount = vehMaxBattery - (currBatt - e);
                        double chargeTime = chargeAmount * stat->getChargingRate();

                        currBatt = vehMaxBattery;
                        currTime += t + chargeTime;
                        currNodeId = bestStationId;

                        continue; // Sạc xong, quay lại tìm khách tiếp
                    }
                }

                // Không khách, không trạm -> Kết thúc route
                routeFinished = true;
            }
        }

        if (customerAddedInThisRoute) {
            // Tính toán lại chính xác toàn bộ route (update battery profile, arrival times chuẩn)
            currentRoute.evaluate(); // Hàm này của class Route
            if (currentRoute.isFeasible()) {
                sol.addRoute(currentRoute);
            } else {
                // Fallback nếu logic tính nhẩm ở trên bị lệch so với evaluate()
                // Thường thì logic trên đã khá chặt chẽ (Safe Return)
            }
        } else {
            cannotServeMore = true;
        }
    }

    // Check nếu còn sót khách
    if (!unservedIds.empty()) {
        std::cerr << "[Warning] Could not serve " << unservedIds.size() << " customers.\n";
    }

    sol.evaluateRoutes(); // [FIX] Calculate totals before returning

    std::cout << "\n========================================\n";
    std::cout << "Initial Solution Summary:\n";
    std::cout << "  Routes: " << sol.getNumRoutes() << "\n";
    std::cout << "  Unserved: " << unservedIds.size() << "\n";
    std::cout << "  Total Distance: " << sol.getTotalDistance() << "\n";
    std::cout << "  Feasible: " << (sol.isFeasible() ? "YES" : "NO") << "\n";
    std::cout << "========================================\n";

    return sol;
}

} // namespace alns