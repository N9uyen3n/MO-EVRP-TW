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
Solution ALNS::createInitialSolution() {
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
        const int maxStepsWithoutCustomer = 3; // Giới hạn số lần sạc liên tiếp

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


// --- 2. Hàm Solve() Chính (Algorithm 2) [cite: 468] ---
std::vector<Solution> ALNS::solve() {
    std::cout << "[LOG] ==== Starting ALNS Execution ====" << std::endl;

    currentSolution = createInitialSolution();
    bestSolution = currentSolution;
    std::cout << "[LOG] Initial solution: " << bestSolution.getNumRoutes()
              << " routes, Total distance: " << bestSolution.getTotalDistance() << std::endl;
    currentSolution.toString();


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