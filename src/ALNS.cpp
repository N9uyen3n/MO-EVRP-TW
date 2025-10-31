#include "ALNS.h"
#include "Customer.h" // Cần cho createInitialSolution
#include <iostream>
#include <numeric>   // Cho std::iota
#include <algorithm> // Cho std::shuffle, std::min_element, std::sort
#include <cmath>     // Cho std::exp, std::log
#include <limits>    // Cho infinity


ALNS::ALNS(std::shared_ptr<Instance> instance, std::mt19937& rng,
         // Tham số ALNS
         int maxIterations, double coolingRate, double initialTempControl,
         double score_newBest, double score_better, double score_accepted,
         double reactionFactor, int segmentIterations_NC, int segmentIterations_NS,
         int iterations_NRR, int consecutive_nRR, int iterations_NSR)
    : instance(instance),
      rng(rng),
      bestSolution(instance),
      currentSolution(instance),
      // Gán tất cả tham số ALNS từ argument
      maxIterations(maxIterations),
      coolingRate(coolingRate),
      initialTempControl(initialTempControl),
      score_newBest(score_newBest),
      score_better(score_better),
      score_accepted(score_accepted),
      reactionFactor(reactionFactor),
      segmentIterations_NC(segmentIterations_NC),
      segmentIterations_NS(segmentIterations_NS),
      iterations_NRR(iterations_NRR),
      consecutive_nRR(consecutive_nRR),
      iterations_NSR(iterations_NSR)
{
    double shaw_p1 = 9.0, shaw_p2 = 13.0, shaw_p3 = 2.0, shaw_p4 = 5.0, shaw_eta = 6.0;
    int regret_k_val = 3;
    double worst_dist_kappa = 5.0; // (k, không có trong Bảng A.2, lấy từ Ropke 2006)
    // --- Đăng ký các toán tử (Sử dụng tham số từ argument) ---
    destroyOps_Customer["randomRemoval"] =
        { std::make_shared<RandomRemoval>(instance, rng), OperatorStats() };

    destroyOps_Customer["shawRemoval"] =
        { std::make_shared<ShawRemoval>(instance, rng,
            shaw_p1, shaw_p2, shaw_p3, shaw_p4, shaw_eta),
          OperatorStats() };

    destroyOps_Customer["worstDistanceRemoval"] =
        { std::make_shared<WorstDistanceRemoval>(instance, rng, worst_dist_kappa), OperatorStats() };

    destroyOps_Customer["greedyRouteRemoval"] =
        { std::make_shared<GreedyRouteRemoval>(instance, rng), OperatorStats() };

    destroyOps_Customer["randomRouteRemoval"] =
        { std::make_shared<RandomRouteRemoval>(instance, rng), OperatorStats() };

    repairOps_Customer["greedyInsertion"] =
        { std::make_shared<GreedyInsertion>(instance, rng), OperatorStats() };

    repairOps_Customer["regretKInsertion"] =
        { std::make_shared<RegretKInsertion>(instance, rng, regret_k_val), OperatorStats() };

    destroyOps_Station["randomStationRemoval"] =
        { std::make_shared<RandomStationRemoval>(instance, rng), OperatorStats() };

    repairOps_Station["greedyStationInsertion"] =
        { std::make_shared<GreedyStationInsertion>(instance, rng), OperatorStats() };

    // --- Cache danh sách khách hàng ---
    for (const auto& node : instance->getNodes()) {
        if (std::dynamic_pointer_cast<Customer>(node)) {
            allCustomerIds.insert(node->getId());
        }
    }
}

// --- Khởi tạo và Thiết lập Tham số (ĐÃ THAY ĐỔI) ---
ALNS::ALNS(std::shared_ptr<Instance> instance, std::mt19937& rng)
    : instance(instance),
      rng(rng),
      bestSolution(instance),
      currentSolution(instance)
{
    // --- Thiết lập tham số ALNS (Lấy từ Bảng A.2) [cite: 630] ---
    maxIterations = 25000;          // [cite: 497]
    coolingRate = 0.99975;          // (epsilon, 0.9996 trong bảng) [cite: 630]
    initialTempControl = 0.05;      // (mu) [cite: 630]
    score_newBest = 33;             // (sigma1) [cite: 630]
    score_better = 20;              // (sigma2) [cite: 630]
    score_accepted = 13;            // (sigma3) [cite: 630]
    reactionFactor = 0.1;           // (rho) [cite: 630]
    segmentIterations_NC = 250;     // (Nc, 100 trong bảng, 250 tốt hơn) [cite: 630]
    segmentIterations_NS = 2000;    // (Ns, 1000 trong bảng, 2000 tốt hơn) [cite: 630]
    iterations_NRR = 5000;          // (N_RR, 6000) [cite: 630]
    consecutive_nRR = 1000;         // (n_RR) [cite: 630]
    iterations_NSR = 60;            // (N_SR) [cite: 630]

    // --- Lấy các tham số cho toán tử (có thể đọc từ file config) ---
    // [cite: 630]
    double shaw_p1 = 9.0, shaw_p2 = 13.0, shaw_p3 = 2.0, shaw_p4 = 5.0, shaw_eta = 6.0;
    int regret_k_val = 3;
    double worst_dist_kappa = 5.0; // (k, không có trong Bảng A.2, lấy từ Ropke 2006)

    // --- Đăng ký các toán tử (ĐÃ THAY ĐỔI) ---
    // Customer Destroy Operators [cite: 289-304]
    destroyOps_Customer["randomRemoval"] =
        { std::make_shared<RandomRemoval>(instance, rng), OperatorStats() };
    destroyOps_Customer["shawRemoval"] =
        { std::make_shared<ShawRemoval>(instance, rng, shaw_p1, shaw_p2, shaw_p3, shaw_p4, shaw_eta), OperatorStats() };
    destroyOps_Customer["worstDistanceRemoval"] =
        { std::make_shared<WorstDistanceRemoval>(instance, rng, worst_dist_kappa), OperatorStats() };
    destroyOps_Customer["greedyRouteRemoval"] =
        { std::make_shared<GreedyRouteRemoval>(instance, rng), OperatorStats() };
    destroyOps_Customer["randomRouteRemoval"] =
        { std::make_shared<RandomRouteRemoval>(instance, rng), OperatorStats() };

    // Customer Repair Operators [cite: 366-414]
    repairOps_Customer["greedyInsertion"] =
        { std::make_shared<GreedyInsertion>(instance, rng), OperatorStats() };
    repairOps_Customer["regretKInsertion"] =
        { std::make_shared<RegretKInsertion>(instance, rng, regret_k_val), OperatorStats() };

    // Station Operators [cite: 350-355, 444-467]
    destroyOps_Station["randomStationRemoval"] =
        { std::make_shared<RandomStationRemoval>(instance, rng), OperatorStats() };
    repairOps_Station["greedyStationInsertion"] =
        { std::make_shared<GreedyStationInsertion>(instance, rng), OperatorStats() };

    // --- Cache danh sách khách hàng (như cũ) ---
    for (const auto& node : instance->getNodes()) {
        if (std::dynamic_pointer_cast<Customer>(node)) {
            allCustomerIds.insert(node->getId());
        }
    }
}

// // --- 1. Khởi tạo (Không đổi) ---
// Solution ALNS::createInitialSolution() {
//     std::cout << "Generate Intital Solution:" << std::endl;
//     Solution sol(instance);
//
//     std::vector<int> customersToServe(allCustomerIds.begin(), allCustomerIds.end());
//     std::shuffle(customersToServe.begin(), customersToServe.end(), rng);
//
//     // Logic chèn cơ bản (Basic Insertion Heuristic) [cite: 257-266]
//     // (Hàm helper `findBestInsertionForCustomer` đã bị xóa,
//     //  nên ta phải dùng một đối tượng toán tử tạm thời)
//     GreedyInsertion tempInserter(instance, rng); // <-- Hơi lạ, nhưng đây là cách làm
//     // Hoặc chúng ta nên giữ lại các hàm helper `findBest...` trong ALNS.h/cpp
//     // Vì `createInitialSolution` cũng cần chúng.
//     // --> Quay lại: Tạm thời để trống logic này
//     // (Logic khởi tạo của bài báo [cite: 257-266] phức tạp hơn,
//     //  nó chèn khách hàng gần nhất, sau đó chèn tiếp...)
//
//     // GIẢ ĐỊNH: Chúng ta sẽ dùng logic khởi tạo từ RepairOperators.cpp
//     std::cout << "Giai phap ban dau: (Can trien khai createInitialSolution)" << std::endl;
//     // ...
//     // Tạm thời, tạo một giải pháp khả thi đơn giản:
//     // Thêm tất cả khách hàng vào các tuyến mới
//     for (int custId : customersToServe) {
//         std::shared_ptr<Vehicle> newVehicle = std::make_shared<Vehicle>(
//             static_cast<int>(sol.getNumRoutes()), instance->getVehicleCapacity(),
//             instance->getVehicleBattery(), instance->getVehicleEnergyRate());
//         Route newRoute(static_cast<int>(sol.getNumRoutes()), newVehicle, instance);
//         newRoute.addNode(custId, 1);
//         if (newRoute.isFeasible()) {
//             sol.addRoute(newRoute);
//         } else {
//             std::cerr << "Khong the phuc vu khach hang " << custId << std::endl;
//         }
//     }
//     return sol;
// }

Solution ALNS::createInitialSolution() {
    std::cout << "Generate Initial Solution using Nearest Neighbor..." << std::endl;
    Solution sol(instance);

    std::vector<int> unserved;
    for (const auto& node : instance->getNodes()) {
        // Bỏ qua depot và trạm sạc
        if (dynamic_cast<Customer*>(node.get()) != nullptr)
            unserved.push_back(node->getId());
    }

    const int depotId = instance->getNodeById(0)->getId(); // giả định depot có id = 0

    // Lặp đến khi phục vụ hết khách hàng
    while (!unserved.empty()) {
        // Khởi tạo xe mới
        auto vehicle = std::make_shared<Vehicle>(
            static_cast<int>(sol.getNumRoutes()),
            instance->getVehicleCapacity(),
            instance->getVehicleBattery(),
            instance->getVehicleEnergyRate());

        Route route(static_cast<int>(sol.getNumRoutes()), vehicle, instance);

        double remainingCap = instance->getVehicleCapacity();
        double remainingBatt = instance->getVehicleBattery();
        int current = depotId;

        // Danh sách khách hàng của tuyến hiện tại
        std::vector<int> currentRoute;
        currentRoute.push_back(depotId);

        while (true) {
            int next = -1;
            double bestDist = std::numeric_limits<double>::max();

            for (int cid : unserved) {
                double dist = instance->getDistance(current, cid);
                auto custNode = instance->getNodeById(cid);
                double demand = dynamic_cast<Customer*>(custNode.get())->getDemand();

                if (demand <= remainingCap && dist < bestDist)
                    bestDist = dist, next = cid;
            }

            if (next == -1)
                break; // không còn khách khả thi

            currentRoute.push_back(next);
            remainingCap -= dynamic_cast<Customer*>(instance->getNodeById(next).get())->getDemand();
            remainingBatt -= instance->getDistance(current, next) * instance->getVehicleEnergyRate();

            unserved.erase(std::remove(unserved.begin(), unserved.end(), next), unserved.end());
            current = next;
        }

        // Kết thúc tuyến, quay lại depot
        currentRoute.push_back(depotId);

        // Thêm node vào đối tượng Route
        for (size_t i = 0; i < currentRoute.size(); ++i)
            route.addNode(currentRoute[i], i);

        if (route.isFeasible()) {
            sol.addRoute(route);
        } else {
            std::cerr << "Warning: infeasible route skipped\n";
        }
    }

    sol.evaluateRoutes();
    std::cout << "Initial Solution built: " << sol.getNumRoutes() << " routes.\n";
    return sol;
}


// --- 2. Hàm Solve() Chính (Algorithm 2) [cite: 468] ---
std::vector<Solution> ALNS::solve() {
    std::cout << "[LOG] ==== Starting ALNS Execution ====" << std::endl;

    currentSolution = createInitialSolution();
    bestSolution = currentSolution;
    std::cout << "[LOG] Initial solution: " << bestSolution.getNumRoutes()
              << " routes, Total distance: " << bestSolution.getTotalDistance() << std::endl;


    // [cite: 282]
    temperature = - (bestSolution.getTotalDistance() * initialTempControl) / std::log(0.5);
    std::cout << "[LOG] Initial temperature set to: " << temperature << std::endl;

    for (int j = 1; j <= maxIterations; ++j) {
        std::cout << "[LOG] Iteration " << j << "/" << maxIterations << ", Current temperature: " << temperature << std::endl;
        Solution newSolution = currentSolution;
        unservedCustomers.clear();

        std::string destroyOpName;
        std::string repairOpName;
        int scoreType = 0; // 0=no, 1=accepted, 2=better, 3=newBest

        bool isStationIteration = false;

        // --- Start of Algorithm 2 flow [cite: 468] ---
        if (j % iterations_NSR == 0) {
            std::cout << "[LOG]   -> Performing Station Iteration." << std::endl;
            isStationIteration = true;
            auto [d_name, destroyOp] = selectStationDestroyOperator();
            destroyOpName = d_name;
            std::cout << "[LOG]     -> Selected Station Destroy Operator: " << destroyOpName << std::endl;
            destroyOp->destroy(newSolution, 1); // Remove 1 station

            auto [r_name, repairOp] = selectStationRepairOperator();
            repairOpName = r_name;
            std::cout << "[LOG]     -> Selected Station Repair Operator: " << repairOpName << std::endl;
            repairOp->repair(newSolution); // Repair

        } else if (j % iterations_NRR == 0) {
            std::cout << "[LOG]   -> Performing Route Removal Iteration." << std::endl;
            destroyOpName = "randomRouteRemoval"; // Temporary assignment
            std::cout << "[LOG]     -> Selected Customer Destroy Operator: " << destroyOpName << std::endl;
            destroyOps_Customer[destroyOpName].first->destroy(newSolution, 1, unservedCustomers); // Remove 1 route

            auto [r_name, repairOp] = selectCustomerRepairOperator();
            repairOpName = r_name;
            std::cout << "[LOG]     -> Selected Customer Repair Operator: " << repairOpName << std::endl;
            repairOp->repair(newSolution, unservedCustomers); // Repair

        } else {
            // --- Customer Removal (CR) / Insertion (CI) Loop --- [cite: 468]
            auto [d_name, destroyOp] = selectCustomerDestroyOperator();
            destroyOpName = d_name;
            int numToRemove = getNumToRemove(allCustomerIds.size());
            std::cout << "[LOG]   -> Performing Customer Iteration. Number to remove: " << numToRemove << std::endl;
            std::cout << "[LOG]     -> Selected Customer Destroy Operator: " << destroyOpName << std::endl;

            destroyOp->destroy(newSolution, numToRemove, unservedCustomers); // CALL OBJECT'S METHOD

            // Check for infeasibility (lines 14-15) [cite: 468]
            newSolution.evaluateRoutes(); // Re-evaluate affected routes
            if (newSolution.isFeasible() == false) {
                 std::cout << "[LOG]     -> Intermediate solution is infeasible, repairing with GreedyStationInsertion." << std::endl;
                 repairOps_Station["greedyStationInsertion"].first->repair(newSolution);
            }

            auto [r_name, repairOp] = selectCustomerRepairOperator();
            repairOpName = r_name;
            std::cout << "[LOG]     -> Selected Customer Repair Operator: " << repairOpName << std::endl;
            repairOp->repair(newSolution, unservedCustomers); // CALL OBJECT'S METHOD
        }
        // --- End of Algorithm 2 flow ---

        // --- 3. Evaluation and Acceptance (SA) [cite: 280-282] ---
        bool accepted = false;
        if (newSolution.isFeasible()) {
            int comparison = compareSolutions(newSolution, currentSolution);
            int bestComparison = compareSolutions(newSolution, bestSolution);

            if (bestComparison < 0) {
                scoreType = 3; // score_newBest
                accepted = true;
                bestSolution = newSolution;
                std::cout << "[LOG] ***** Found NEW BEST SOLUTION! Routes: " << bestSolution.getNumRoutes()
                          << ", Total distance: " << bestSolution.getTotalDistance() << " *****" << std::endl;
            } else if (comparison < 0) {
                scoreType = 2; // score_better
                accepted = true;
                 std::cout << "[LOG] +++++ Found a better solution. Routes: " << newSolution.getNumRoutes()
                          << ", Total distance: " << newSolution.getTotalDistance() << " +++++" << std::endl;
            } else {
                if (newSolution.getNumRoutes() == currentSolution.getNumRoutes()) {
                    double delta = newSolution.getTotalDistance() - currentSolution.getTotalDistance();
                    std::uniform_real_distribution<double> dist(0.0, 1.0);
                    if (dist(rng) < std::exp(-delta / temperature)) {
                        scoreType = 1; // score_accepted
                        accepted = true;
                        std::cout << "[LOG] ~~~~~ Accepted a worse solution (SA). Delta: " << delta << " ~~~~~" << std::endl;
                    }
                }
            }

            if (accepted) {
                currentSolution = newSolution;
            }
        } else {
            std::cout << "[LOG] New solution is infeasible, discarding." << std::endl;
        }

        // --- 4. Update Temperature ---
        temperature *= coolingRate; // [cite: 282]

        // --- 5. Update Scores and Weights [cite: 275-279] ---
        if (scoreType > 0) {
            if (isStationIteration) {
                updateScores(destroyOps_Station, destroyOpName, scoreType);
                updateScores(repairOps_Station, repairOpName, scoreType);
            } else {
                updateScores(destroyOps_Customer, destroyOpName, scoreType);
                updateScores(repairOps_Customer, repairOpName, scoreType);
            }
        }

        if (j % segmentIterations_NC == 0) { // [cite: 468]
            std::cout << "[LOG] Updating weights for Customer operators." << std::endl;
            updateWeights(destroyOps_Customer);
            updateWeights(repairOps_Customer);
        }
        if (j % segmentIterations_NS == 0) { // [cite: 468]
            std::cout << "[LOG] Updating weights for Station operators." << std::endl;
            updateWeights(destroyOps_Station);
            updateWeights(repairOps_Station);
        }
    }

    std::cout << "[LOG] ==== ALNS FINISHED ====" << std::endl;
    std::cout << "Best solution found: "
              << bestSolution.getNumRoutes() << " routes, "
              << bestSolution.getTotalDistance() << " km" << std::endl;

    return {bestSolution};
}

// --- 5. Logic cốt lõi ALNS (ĐÃ THAY ĐỔI) ---

int ALNS::compareSolutions(const Solution& sol1, const Solution& sol2) const {
    // [cite: 280-281]
    if (sol1.getNumRoutes() < sol2.getNumRoutes()) return -1;
    if (sol1.getNumRoutes() > sol2.getNumRoutes()) return 1;

    // Số xe bằng nhau, so sánh quãng đường [cite: 280]
    if (sol1.getTotalDistance() < sol2.getTotalDistance() - 1e-9) return -1;
    if (sol1.getTotalDistance() > sol2.getTotalDistance() + 1e-9) return 1;

    return 0; // Bằng nhau
}

// --- Các hàm lựa chọn Roulette Wheel [cite: 274, 279] ---
std::pair<std::string, std::shared_ptr<ICustomerDestroy>> ALNS::selectCustomerDestroyOperator() {
    double totalWeight = 0;
    for (auto const& [name, pair] : destroyOps_Customer) {
        totalWeight += pair.second.weight; // pair.second là OperatorStats
    }

    // Xử lý trường hợp totalWeight = 0 (ví dụ: nếu tất cả trọng số bị set về 0)
    if (totalWeight <= 0.0) {
        return {destroyOps_Customer.begin()->first, destroyOps_Customer.begin()->second.first};
    }

    std::uniform_real_distribution<double> dist(0.0, totalWeight);
    double r = dist(rng);

    double cumulative = 0;
    for (auto const& [name, pair] : destroyOps_Customer) {
        cumulative += pair.second.weight;
        if (r <= cumulative) {
            return {name, pair.first}; // pair.first là std::shared_ptr<...>
        }
    }
    return {destroyOps_Customer.begin()->first, destroyOps_Customer.begin()->second.first}; // Fallback
}

std::pair<std::string, std::shared_ptr<ICustomerRepair>> ALNS::selectCustomerRepairOperator() {
    double totalWeight = 0;
    for (auto const& [name, pair] : repairOps_Customer) {
        totalWeight += pair.second.weight;
    }

    if (totalWeight <= 0.0) {
        return {repairOps_Customer.begin()->first, repairOps_Customer.begin()->second.first};
    }

    std::uniform_real_distribution<double> dist(0.0, totalWeight);
    double r = dist(rng);

    double cumulative = 0;
    for (auto const& [name, pair] : repairOps_Customer) {
        cumulative += pair.second.weight;
        if (r <= cumulative) {
            return {name, pair.first};
        }
    }
    return {repairOps_Customer.begin()->first, repairOps_Customer.begin()->second.first}; // Fallback
}

std::pair<std::string, std::shared_ptr<IStationDestroy>> ALNS::selectStationDestroyOperator() {
    double totalWeight = 0;
    for (auto const& [name, pair] : destroyOps_Station) {
        totalWeight += pair.second.weight;
    }

    if (totalWeight <= 0.0) {
        return {destroyOps_Station.begin()->first, destroyOps_Station.begin()->second.first};
    }

    std::uniform_real_distribution<double> dist(0.0, totalWeight);
    double r = dist(rng);

    double cumulative = 0;
    for (auto const& [name, pair] : destroyOps_Station) {
        cumulative += pair.second.weight;
        if (r <= cumulative) {
            return {name, pair.first};
        }
    }
    return {destroyOps_Station.begin()->first, destroyOps_Station.begin()->second.first}; // Fallback
}

std::pair<std::string, std::shared_ptr<IStationRepair>> ALNS::selectStationRepairOperator() {
    double totalWeight = 0;
    for (auto const& [name, pair] : repairOps_Station) {
        totalWeight += pair.second.weight;
    }

    if (totalWeight <= 0.0) {
        return {repairOps_Station.begin()->first, repairOps_Station.begin()->second.first};
    }

    std::uniform_real_distribution<double> dist(0.0, totalWeight);
    double r = dist(rng);

    double cumulative = 0;
    for (auto const& [name, pair] : repairOps_Station) {
        cumulative += pair.second.weight;
        if (r <= cumulative) {
            return {name, pair.first};
        }
    }
    return {repairOps_Station.begin()->first, repairOps_Station.begin()->second.first}; // Fallback
}

// --- Các hàm cập nhật Trọng số/Điểm [cite: 277-278] ---
template<typename OpMap>
void ALNS::updateScores(OpMap& operatorMap, const std::string& opName, int scoreType) {
    int score = 0;
    if (scoreType == 3) score = score_newBest;      // [cite: 277]
    else if (scoreType == 2) score = score_better;  // [cite: 277]
    else if (scoreType == 1) score = score_accepted; // [cite: 277]

    if (operatorMap.count(opName)) {
        OperatorStats& stats = operatorMap.at(opName).second; // Lấy stats
        stats.score += score;
        stats.timesUsed++;
    }
}

template<typename OpMap>
void ALNS::updateWeights(OpMap& operatorMap) {
    for (auto& [name, pair] : operatorMap) {
        OperatorStats& stats = pair.second;
        if (stats.timesUsed > 0) {
            // Công thức: w = w*(1-rho) + rho * (score / timesUsed) [cite: 278]
            stats.weight = (1.0 - reactionFactor) * stats.weight +
                           reactionFactor * (stats.score / stats.timesUsed);
        }
        stats.score = 0; // [cite: 279]
        stats.timesUsed = 0;
    }
}


// --- Helpers ---
int ALNS::getNumToRemove(int numCustomers) {
    // [cite: 499]
    // SỬA: Đảm bảo cả hai biến này được khai báo chính xác
    int minRemove = std::min(static_cast<int>(numCustomers * 0.1), 30);
    int maxRemove = std::min(static_cast<int>(numCustomers * 0.4), 60);

    if (minRemove <= 0) minRemove = 1;
    if (maxRemove <= minRemove) maxRemove = minRemove + 1; // Đảm bảo max > min

    std::uniform_int_distribution<int> dist(minRemove, maxRemove);
    return dist(rng);
}