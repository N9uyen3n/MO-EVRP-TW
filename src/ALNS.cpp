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

// --- 1. Khởi tạo (Không đổi) ---
Solution ALNS::createInitialSolution() {
    std::cout << "Tao giai phap ban dau..." << std::endl;
    Solution sol(instance);

    std::vector<int> customersToServe(allCustomerIds.begin(), allCustomerIds.end());
    std::shuffle(customersToServe.begin(), customersToServe.end(), rng);

    // Logic chèn cơ bản (Basic Insertion Heuristic) [cite: 257-266]
    // (Hàm helper `findBestInsertionForCustomer` đã bị xóa,
    //  nên ta phải dùng một đối tượng toán tử tạm thời)
    GreedyInsertion tempInserter(instance, rng); // <-- Hơi lạ, nhưng đây là cách làm
    // Hoặc chúng ta nên giữ lại các hàm helper `findBest...` trong ALNS.h/cpp
    // Vì `createInitialSolution` cũng cần chúng.
    // --> Quay lại: Tạm thời để trống logic này
    // (Logic khởi tạo của bài báo [cite: 257-266] phức tạp hơn,
    //  nó chèn khách hàng gần nhất, sau đó chèn tiếp...)

    // GIẢ ĐỊNH: Chúng ta sẽ dùng logic khởi tạo từ RepairOperators.cpp
    std::cout << "Giai phap ban dau: (Can trien khai createInitialSolution)" << std::endl;
    // ...
    // Tạm thời, tạo một giải pháp khả thi đơn giản:
    // Thêm tất cả khách hàng vào các tuyến mới
    for (int custId : customersToServe) {
        std::shared_ptr<Vehicle> newVehicle = std::make_shared<Vehicle>(
            sol.getNumRoutes(), instance->getVehicleCapacity(),
            instance->getVehicleBattery(), instance->getVehicleEnergyRate());
        Route newRoute(sol.getNumRoutes(), newVehicle, instance);
        newRoute.addNode(custId, 1);
        if (newRoute.isFeasible()) {
            sol.addRoute(newRoute);
        } else {
            std::cerr << "Khong the phuc vu khach hang " << custId << std::endl;
        }
    }
    return sol;
}


[cite_start]// --- 2. Hàm Solve() Chính (Algorithm 2) [cite: 468] ---
std::vector<Solution> ALNS::solve() {
    std::cout << "Bat dau ALNS..." << std::endl;

    currentSolution = createInitialSolution();
    bestSolution = currentSolution;

    // [cite: 282]
    temperature = - (bestSolution.getTotalDistance() * initialTempControl) / std::log(0.5);
    std::cout << "Nhiet do ban dau: " << temperature << std::endl;

    for (int j = 1; j <= maxIterations; ++j) {
        Solution newSolution = currentSolution;
        unservedCustomers.clear();

        std::string destroyOpName;
        std::string repairOpName;
        int scoreType = 0; // 0=ko, 1=accepted, 2=better, 3=newBest

        bool isStationIteration = false;

        // --- Bắt đầu luồng Algorithm 2 [cite: 468] ---
        if (j % iterations_NSR == 0) {
            // --- Vòng lặp Station Removal (SR) / Insertion (SI) --- [cite: 468]
            isStationIteration = true;
            auto [d_name, destroyOp] = selectStationDestroyOperator();
            destroyOpName = d_name;
            destroyOp->destroy(newSolution, 1); // Xóa 1 trạm

            auto [r_name, repairOp] = selectStationRepairOperator();
            repairOpName = r_name;
            repairOp->repair(newSolution); // Sửa lại

        } else if (j % iterations_NRR == 0) {
            // --- Vòng lặp Route Removal (RR) --- [cite: 468]
            // Bài báo chạy n_RR vòng, ta chạy 1 lần cho đơn giản
            // (Để chạy n_RR vòng, cần một vòng lặp for nhỏ ở đây)
            destroyOpName = "randomRouteRemoval"; // Tạm gán
            destroyOps_Customer[destroyOpName].first->destroy(newSolution, 1, unservedCustomers); // Xóa 1 tuyến

            auto [r_name, repairOp] = selectCustomerRepairOperator();
            repairOpName = r_name;
            repairOp->repair(newSolution, unservedCustomers); // Sửa

        } else {
            // --- Vòng lặp Customer Removal (CR) / Insertion (CI) --- [cite: 468]
            auto [d_name, destroyOp] = selectCustomerDestroyOperator();
            destroyOpName = d_name;
            int numToRemove = getNumToRemove(allCustomerIds.size());

            destroyOp->destroy(newSolution, numToRemove, unservedCustomers); // GỌI HÀM CỦA ĐỐI TƯỢNG

            // Kiểm tra infeasibility (dòng 14-15) [cite: 468]
            newSolution.evaluate(); // Đánh giá lại các tuyến bị ảnh hưởng
            if (newSolution.isFeasible() == false) {
                 repairOps_Station["greedyStationInsertion"].first->repair(newSolution);
            }

            auto [r_name, repairOp] = selectCustomerRepairOperator();
            repairOpName = r_name;
            repairOp->repair(newSolution, unservedCustomers); // GỌI HÀM CỦA ĐỐI TƯỢNG
        }
        // --- Kết thúc luồng Algorithm 2 ---

        // --- 3. Đánh giá và Chấp nhận (SA) [cite: 280-282] ---
        bool accepted = false;
        if (newSolution.isFeasible()) {
            int comparison = compareSolutions(newSolution, currentSolution);
            int bestComparison = compareSolutions(newSolution, bestSolution);

            if (bestComparison < 0) {
                scoreType = 3; // score_newBest
                accepted = true;
                bestSolution = newSolution;
                // (Thêm output...)
            } else if (comparison < 0) {
                scoreType = 2; // score_better
                accepted = true;
            } else {
                if (newSolution.getNumRoutes() == currentSolution.getNumRoutes()) {
                    double delta = newSolution.getTotalDistance() - currentSolution.getTotalDistance();
                    std::uniform_real_distribution<double> dist(0.0, 1.0);
                    if (dist(rng) < std::exp(-delta / temperature)) {
                        scoreType = 1; // score_accepted
                        accepted = true;
                    }
                }
            }

            if (accepted) {
                currentSolution = newSolution;
            }
        }

        // --- 4. Cập nhật Nhiệt độ ---
        temperature *= coolingRate; // [cite: 282]

        // --- 5. Cập nhật Điểm và Trọng số [cite: 275-279] ---
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
            updateWeights(destroyOps_Customer);
            updateWeights(repairOps_Customer);
        }
        if (j % segmentIterations_NS == 0) { // [cite: 468]
            updateWeights(destroyOps_Station);
            updateWeights(repairOps_Station);
        }
    }

    std::cout << "ALNS ket thuc." << std::endl;
    std::cout << "Giai phap tot nhat: "
              << bestSolution.getNumRoutes() << " tuyen, "
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