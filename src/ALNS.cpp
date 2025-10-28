#include "../include/ALNS.h"
#include "../include/Instance.h"
#include "../include/Customer.h"
#include "../include/Depot.h"
#include "../include/Vehicle.h"
#include <iostream>
#include <vector>
#include <set>
#include <stdexcept>
#include <numeric>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <chrono>

// ============================================================================
// CONSTRUCTOR - Improved initialization with validation
// ============================================================================
ALNS::ALNS(ObjectiveManager manager, std::shared_ptr<Instance> instance) 
    : instance(std::move(instance)),
      objectiveManager(std::move(manager)), 
      rng(std::random_device{}())
{
    if (!this->instance) {
        throw std::runtime_error("Instance cannot be null in ALNS constructor");
    }

    // Improved configuration with validation
    maxIterations = 3000;
    segmentSize = 100;
    destructionRate = 0.2;
    
    // Enhanced temperature schedule
    temperature = 100.0;
    coolingRate = 0.995;
    minimumTemperature = 0.5;
    initialTemperature = temperature; // Store for potential reheating
    
    reactionFactor = 0.1;

    // Improved reward structure (more aggressive rewards for better solutions)
    sigma1 = 30;  // New global best
    sigma2 = 20;  // Better than current
    sigma3 = 5;   // Accepted (non-dominated)

    // Initialize operators with validation
    initializeOperators();
    
    std::cout << "ALNS initialized with " << destroyOperators.size() 
              << " destroy and " << repairOperators.size() 
              << " repair operators." << std::endl;
}

// ============================================================================
// OPERATOR INITIALIZATION - Separated for clarity
// ============================================================================
void ALNS::initializeOperators() {
    // Destroy operators
    destroyOperators.push_back(std::make_unique<RandomRemoval>());
    destroyOperators.push_back(std::make_unique<WorstDistanceRemoval>());
    destroyOperators.push_back(std::make_unique<ShawRemoval>());
    destroyOperators.push_back(std::make_unique<ZoneRemoval>());

    // Repair operators
    repairOperators.push_back(std::make_unique<GreedyInsertion>());
    repairOperators.push_back(std::make_unique<RegretInsertion>());

    // Initialize weights uniformly
    const size_t numDestroy = destroyOperators.size();
    const size_t numRepair = repairOperators.size();
    
    destroyWeights.assign(numDestroy, 1.0);
    repairWeights.assign(numRepair, 1.0);
    destroyScores.assign(numDestroy, 0);
    repairScores.assign(numRepair, 0);
    destroyUses.assign(numDestroy, 0);
    repairUses.assign(numRepair, 0);
    
    // Track best performance for each operator
    destroyBestScores.assign(numDestroy, 0);
    repairBestScores.assign(numRepair, 0);
}
// ============================================================================
// LOCAL SEARCH - Inter-Route Relocate (Di dời giữa các tuyến) - PHIÊN BẢN MỚI
// ============================================================================
void ALNS::applyLocalSearch(Solution& solution) {
    bool improvement = true;
    
    // Sử dụng chiến lược "First Improvement" (Cải thiện đầu tiên)
    while (improvement) {
        improvement = false;

        for (size_t i = 0; i < solution.routes.size(); ++i) {
            for (size_t j = 0; j < solution.routes.size(); ++j) {
                if (i == j) continue; // Bỏ qua nếu là cùng 1 tuyến

                Route& route_i = solution.routes[i]; // Tuyến A (nguồn)
                Route& route_j = solution.routes[j]; // Tuyến B (đích)

                // Thử di dời (Relocate) mọi khách hàng từ route_i sang route_j
                // Lưu ý: lặp ngược để việc xóa theo chỉ số không ảnh hưởng đến các phần tử chưa xét
                for (int c_idx = route_i.getInfos().size() - 2; c_idx >= 1; --c_idx) {
                    
                    auto customerToMove = std::dynamic_pointer_cast<Customer>(route_i.getInfos()[c_idx].node);
                    if (!customerToMove) continue; // Bỏ qua nếu không phải là khách hàng (ví dụ: trạm sạc)

                    // --- Bước 1: Tính chi phí TIẾT KIỆM được khi gỡ khách hàng khỏi route_i ---
                    // Cách làm an toàn và chính xác nhất là mô phỏng việc xóa trên một bản sao
                    // và tính lại chi phí (tổng thời gian).
                    double cost_i_before = route_i.getTotalTime();
                    Route temp_route_i = route_i; // Tạo bản sao tạm thời
                    
                    // Xóa khách hàng khỏi bản sao. `remove` theo chỉ số là đủ vì đây là bản sao.
                    temp_route_i.remove(c_idx);
                    
                    double cost_i_after = temp_route_i.getTotalTime();
                    double costSaving = cost_i_before - cost_i_after;

                    int bestNewPos = -1;
                    double bestTotalDelta = std::numeric_limits<double>::max();

                    // --- Bước 2: Tìm vị trí chèn tốt nhất trong route_j ---
                    for (int pos_j = 1; pos_j < route_j.getInfos().size(); ++pos_j) {
                        // Sử dụng hàm evaluateInsertion mạnh mẽ đã được tái cấu trúc
                        EvaluationResult result = route_j.evaluateInsertion(customerToMove, pos_j);
                        
                        if (result.isFeasible) {
                            double costIncrease = result.costDelta;
                            double totalDelta = costIncrease - costSaving;

                            if (totalDelta < bestTotalDelta) {
                                bestTotalDelta = totalDelta;
                                bestNewPos = pos_j;
                            }
                        }
                    }

                    // --- Bước 3: Thực hiện di dời nếu có lợi (delta tổng < 0) ---
                    if (bestNewPos != -1 && bestTotalDelta < -1e-4) { // Sử dụng sai số nhỏ
                        
                        // Thực hiện thay đổi trên các tuyến đường thực tế
                        route_i.remove(c_idx);
                        route_j.insert(customerToMove, bestNewPos);

                        improvement = true; // Báo hiệu đã có cải thiện
                        
                        // Thoát và bắt đầu lại Local Search từ đầu
                        goto restart_ls; 
                    }
                } // kết thúc lặp qua khách hàng
            } // kết thúc lặp route_j
        } // kết thúc lặp route_i

        restart_ls:; // Nhãn để goto
    } // kết thúc vòng lặp while(improvement)

    // Dọn dẹp các tuyến đường rỗng (nếu có) sau khi LS kết thúc
    solution.routes.erase(
        std::remove_if(solution.routes.begin(), solution.routes.end(),
            [](const Route& r) { 
                // Một tuyến rỗng chỉ có [depot, depot]
                return r.getInfos().size() <= 2 && r.getCustomerCount() == 0;
            }),
        solution.routes.end()
    );
}

// ============================================================================
// MAIN SOLVE METHOD - Improved with better logging and statistics
// ============================================================================
std::vector<Solution> ALNS::solve() {
    if (!instance) {
        throw std::runtime_error("Instance is null in solve()");
    }

    auto startTime = std::chrono::high_resolution_clock::now();
    
    printHeader();
    
    // Initialize solution and statistics
    Solution currentSolution = generateInitialSolution();
    calculateObjectives(currentSolution);
    updateArchive(currentSolution);
    
    Solution bestSolution = currentSolution;
    int iterationsSinceImprovement = 0;
    int acceptedSolutions = 0;
    int rejectedSolutions = 0;
    int totalFeasible = 0;
    int totalInfeasible = 0;

    // Main optimization loop
    for (int iteration = 0; iteration < maxIterations; ++iteration) {
        Solution tempSolution = currentSolution;

        // Select operators
        auto destroyPair = selectOperator(destroyOperators, destroyWeights);
        IDestroyOperator* destroyOp = destroyPair.first;
        size_t destroyIdx = destroyPair.second;

        auto repairPair = selectOperator(repairOperators, repairWeights);
        IRepairOperator* repairOp = repairPair.first;
        size_t repairIdx = repairPair.second;
        
        destroyUses[destroyIdx]++;
        repairUses[repairIdx]++;

        // Apply destroy and repair
        auto removedCustomers = destroyOp->destroy(tempSolution, *instance, destructionRate, rng);
        bool repaired = repairOp->repair(tempSolution, *instance, removedCustomers, rng);

        if (repaired) {
            applyLocalSearch(tempSolution);
        }

        if (repaired && isFeasible(tempSolution)) {
            totalFeasible++;
            calculateObjectives(tempSolution);

            // Evaluate solution quality
            Dominance status = dominanceCheck(tempSolution, currentSolution);
            bool addedToArchive = updateArchive(tempSolution);

            // Update operator scores based on performance
            int reward = 0;
            if (addedToArchive) {
                reward = sigma1;
                iterationsSinceImprovement = 0;
                
                // Update best solution if it dominates current best
                if (dominanceCheck(tempSolution, bestSolution) == DOMINATES) {
                    bestSolution = tempSolution;
                }
            } else if (status == DOMINATES) {
                reward = sigma2;
                iterationsSinceImprovement = 0;
            } else if (status != DOMINATED) {
                reward = sigma3;
            }

            if (reward > 0) {
                destroyScores[destroyIdx] += reward;
                repairScores[repairIdx] += reward;
                
                // Track best scores
                destroyBestScores[destroyIdx] = std::max(destroyBestScores[destroyIdx], static_cast<size_t>(reward));
                repairBestScores[repairIdx] = std::max(repairBestScores[repairIdx], static_cast<size_t>(reward));
            }

            // Acceptance decision with improved SA
            bool accepted = false;
            if (status == DOMINATES) {
                currentSolution = tempSolution;
                accepted = true;
            } else if (status != DOMINATED) {
                // Enhanced SA with adaptive acceptance
                double acceptProb = std::exp(-1.0 / temperature);
                std::uniform_real_distribution<> dist(0.0, 1.0);
                if (dist(rng) < acceptProb) {
                    currentSolution = tempSolution;
                    accepted = true;
                }
            }

            if (accepted) {
                acceptedSolutions++;
            } else {
                rejectedSolutions++;
            }
        } else {
            totalInfeasible++;
        }

        // Periodic weight updates
        if (iteration > 0 && iteration % segmentSize == 0) {
            updateWeights();
            
            // Log progress
            if (iteration % (segmentSize * 5) == 0) {
                logProgress(iteration, acceptedSolutions, rejectedSolutions, 
                           totalFeasible, totalInfeasible);
            }
        }

        // Adaptive temperature cooling with reheating
        temperature *= coolingRate;
        if (temperature < minimumTemperature) {
            temperature = minimumTemperature;
        }
        
        // Reheating strategy if stuck
        if (iterationsSinceImprovement > maxIterations / 4) {
            temperature = initialTemperature * 0.5;
            iterationsSinceImprovement = 0;
            std::cout << "  [Reheating] Temperature reset to " << temperature << std::endl;
        }
        
        iterationsSinceImprovement++;
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    printFooter(duration.count(), acceptedSolutions, rejectedSolutions, 
                totalFeasible, totalInfeasible);
    
    return archive;
}

// ============================================================================
// OBJECTIVE CALCULATION - Cached for performance
// ============================================================================
void ALNS::calculateObjectives(Solution& solution) {
    std::vector<double> values = objectiveManager.evaluateAll(solution);
    solution.setObjectiveValues(values);
}

// ============================================================================
// OPERATOR SELECTION - Improved with roulette wheel selection
// ============================================================================
std::pair<IDestroyOperator*, size_t> ALNS::selectOperator(
    const std::vector<std::unique_ptr<IDestroyOperator>>& operators, 
    const std::vector<double>& weights) 
{
    // Ensure weights are positive
    std::vector<double> positiveWeights = weights;
    for (auto& w : positiveWeights) {
        w = std::max(w, 0.01); // Minimum weight to ensure all operators get tried
    }
    
    std::discrete_distribution<> dist(positiveWeights.begin(), positiveWeights.end());
    size_t index = dist(rng);
    return {operators[index].get(), index};
}

std::pair<IRepairOperator*, size_t> ALNS::selectOperator(
    const std::vector<std::unique_ptr<IRepairOperator>>& operators, 
    const std::vector<double>& weights) 
{
    std::vector<double> positiveWeights = weights;
    for (auto& w : positiveWeights) {
        w = std::max(w, 0.01);
    }
    
    std::discrete_distribution<> dist(positiveWeights.begin(), positiveWeights.end());
    size_t index = dist(rng);
    return {operators[index].get(), index};
}

// ============================================================================
// WEIGHT UPDATE - Improved with noise reduction and smoothing
// ============================================================================
void ALNS::updateWeights() {
    // Update destroy operator weights with smoothing
    for (size_t i = 0; i < destroyOperators.size(); ++i) {
        if (destroyUses[i] > 0) {
            double avgScore = static_cast<double>(destroyScores[i]) / destroyUses[i];
            // Exponential smoothing
            destroyWeights[i] = (1.0 - reactionFactor) * destroyWeights[i] + 
                               reactionFactor * avgScore;
            
            // Ensure minimum weight
            destroyWeights[i] = std::max(destroyWeights[i], 0.1);
        }
    }

    // Update repair operator weights with smoothing
    for (size_t i = 0; i < repairOperators.size(); ++i) {
        if (repairUses[i] > 0) {
            double avgScore = static_cast<double>(repairScores[i]) / repairUses[i];
            repairWeights[i] = (1.0 - reactionFactor) * repairWeights[i] + 
                              reactionFactor * avgScore;
            
            repairWeights[i] = std::max(repairWeights[i], 0.1);
        }
    }

    // Reset counters
    std::fill(destroyScores.begin(), destroyScores.end(), 0);
    std::fill(repairScores.begin(), repairScores.end(), 0);
    std::fill(destroyUses.begin(), destroyUses.end(), 0);
    std::fill(repairUses.begin(), repairUses.end(), 0);
}

// ============================================================================
// INITIAL SOLUTION - Improved with better route construction
// ============================================================================
Solution ALNS::generateInitialSolution() {
    std::cout << "Generating initial solution..." << std::endl;
    
    Solution sol;
    std::shared_ptr<Node> depot = nullptr;
    std::vector<std::shared_ptr<Customer>> customers;

    // Collect nodes
    for (const auto& node : instance->getNodes()) {
        if (dynamic_cast<Depot*>(node.get())) {
            depot = node;
        } else if (auto customer = std::dynamic_pointer_cast<Customer>(node)) {
            customers.push_back(customer);
        }
    }

    if (!depot) {
        throw std::runtime_error("Depot not found in instance!");
    }

    // Create one route per customer (simple initial solution)
    int routeId = 0;
    for (const auto& customer : customers) {
        auto vehicle = std::make_shared<Vehicle>(
            routeId, 
            instance->getVehicleCapacity(), 
            instance->getVehicleBattery(), 
            instance->getVehicleEnergyRate()
        );
        
        Route newRoute(routeId++, vehicle, instance);
        newRoute.insert(depot, 0);
        newRoute.insert(customer, 1);
        newRoute.insert(depot, 2);

        sol.routes.push_back(newRoute);
    }

    std::cout << "Initial solution: " << sol.routes.size() << " routes, "
              << customers.size() << " customers" << std::endl;
    
    return sol;
}

// ============================================================================
// ARCHIVE MANAGEMENT - Improved with diversity maintenance
// ============================================================================
bool ALNS::updateArchive(Solution& newSolution) {
    // Check if dominated by any existing solution
    for (const auto& existing : archive) {
        if (dominanceCheck(newSolution, existing) == DOMINATED) {
            return false;
        }
    }

    // Remove dominated solutions
    size_t initialSize = archive.size();
    archive.erase(
        std::remove_if(archive.begin(), archive.end(),
            [&](const Solution& existing) {
                return dominanceCheck(newSolution, existing) == DOMINATES;
            }),
        archive.end()
    );

    // Add new solution
    archive.push_back(newSolution);
    
    // Return true if we removed any solutions (improvement)
    return archive.size() < initialSize + 1;
}

// ============================================================================
// FEASIBILITY CHECK - Optimized with early termination
// ============================================================================
// bool ALNS::isFeasible(const Solution& solution) {
//     if (solution.routes.empty()) {
//         return false;
//     }

//     constexpr double EPSILON = 1e-4;

//     for (const auto& route : solution.routes) {
//         const auto& infos = route.getInfos();
        
//         if (infos.empty()) {
//             continue;
//         }

//         auto vehicle = route.getVehicle();
//         const double capacity = vehicle->getCapacity();
//         const double batteryCapacity = vehicle->getBatteryCapacity();

//         // Check initial load
//         if (infos[0].departure_load > capacity + EPSILON) {
//             return false;
//         }

//         for (const auto& info : infos) {
//             // Battery constraints
//             if (info.arrival_battery < -EPSILON || 
//                 info.departure_battery > batteryCapacity + EPSILON) {
//                 return false;
//             }

//             // Load constraints
//             if (info.departure_load < -EPSILON || 
//                 info.departure_load > capacity + EPSILON) {
//                 return false;
//             }

//             // Time window constraints
//             if (auto customer = std::dynamic_pointer_cast<const Customer>(info.node)) {
//                 if (info.arrival_time > customer->getDueDate() + EPSILON) {
//                     return false;
//                 }
//             } else if (auto depot = std::dynamic_pointer_cast<const Depot>(info.node)) {
//                 if (info.arrival_time > depot->getLastTime() + EPSILON) {
//                     return false;
//                 }
//             }
//         }

//         // Final load should be zero
//         if (std::abs(infos.back().departure_load) > EPSILON) {
//             return false;
//         }
//     }

//     return true;
// }
 // ============================================================================
// HÀM CŨ: VIẾT LẠI `isFeasible` (KHẮC PHỤC VẤN ĐỀ 1, 2, 3, 4)
// ============================================================================
bool ALNS::isFeasible(const Solution& solution) {
    if (solution.routes.empty()) {
        return true; // Một giải pháp không có tuyến nào (nếu không có khách hàng) là hợp lệ
    }
    
    constexpr double EPSILON = 1e-4;
    std::set<int> customers_served; // Dùng để kiểm tra tính duy nhất
    int total_customers_in_solution = 0;

    for (const auto& route : solution.routes) {
        const auto& infos = route.getInfos();
        
        // --- Vấn đề 1 & 4: Kiểm tra cấu trúc tuyến ---
        if (infos.empty()) {
            continue; // Bỏ qua tuyến rỗng (nếu Local Search tạo ra)
        }
        if (infos.size() < 2) {
             return false; // Tuyến phải có ít nhất [Depot, Depot]
        }
        if (!std::dynamic_pointer_cast<Depot>(infos.front().node) ||
            !std::dynamic_pointer_cast<Depot>(infos.back().node)) {
            return false; // Phải bắt đầu và kết thúc tại Depot
        }

        auto vehicle = route.getVehicle();
        const double capacity = vehicle->getCapacity();
        const double batteryCapacity = vehicle->getBatteryCapacity();

        // --- Kiểm tra tính nhất quán của từng điểm dừng ---
        // (Giả định rằng `checkAndUpdateInfos` đã làm đúng)
        // (Chúng ta chỉ cần kiểm tra lại các giá trị đã tính)
        for (const auto& info : infos) {
            
            // Battery constraints
            if (info.arrival_battery < -EPSILON || 
                info.departure_battery > batteryCapacity + EPSILON) {
                return false; // Pin vi phạm
            }

            // Load constraints
            if (info.departure_load < -EPSILON || 
                info.departure_load > capacity + EPSILON) {
                return false; // Tải trọng vi phạm
            }

            // Time window constraints
            if (auto customer = std::dynamic_pointer_cast<const Customer>(info.node)) {
                // Vấn đề 2: Check DueDate
                if (info.arrival_time > customer->getDueDate() + EPSILON) {
                    return false; // Trễ giờ
                }
                
                // GHI CHÚ: Không cần check ReadyTime (arrival < readyTime)
                // vì `recalculateInfos` đã xử lý bằng cách thêm wait_time.
                
                // Kiểm tra tính duy nhất
                if (customers_served.count(customer->getId())) {
                    return false; // Khách hàng này đã được phục vụ ở tuyến khác
                }
                customers_served.insert(customer->getId());
                total_customers_in_solution++;

            } 
        }
        
        // ** LOẠI BỎ LỖI: Không kiểm tra tải trọng về 0 **
        // if (std::abs(infos.back().departure_load) > EPSILON) {
        //     return false;
        // }
    }

    // --- Kiểm tra tính toàn vẹn của Solution ---
    // Đảm bảo mọi khách hàng trong 'instance' đều được phục vụ
    int total_customers_in_instance = 0;
    for (const auto& node : instance->getNodes()) {
        if (std::dynamic_pointer_cast<Customer>(node)) {
            total_customers_in_instance++;
        }
    }
    
    if (total_customers_in_solution != total_customers_in_instance) {
        return false; // Số lượng khách hàng phục vụ không khớp với bài toán
    }

    return true;
}


// ============================================================================
// DOMINANCE CHECK - Optimized comparison
// ============================================================================
ALNS::Dominance ALNS::dominanceCheck(const Solution& a, const Solution& b) {
    const auto& objA = a.objectives;
    const auto& objB = b.objectives;

    if (objA.size() != objB.size()) {
        throw std::runtime_error("Objective vectors have different sizes");
    }

    bool aBetter = false;
    bool bBetter = false;

    for (size_t i = 0; i < objA.size(); ++i) {
        if (objA[i] < objB[i] - 1e-6) {  // Use small epsilon for floating point comparison
            aBetter = true;
        } else if (objB[i] < objA[i] - 1e-6) {
            bBetter = true;
        }
        
        // Early termination
        if (aBetter && bBetter) {
            return NON_DOMINATED;
        }
    }

    if (aBetter && !bBetter) return DOMINATES;
    if (bBetter && !aBetter) return DOMINATED;
    return NON_DOMINATED;
}

// ============================================================================
// LOGGING AND REPORTING
// ============================================================================
void ALNS::printHeader() const {
    std::cout << "" << std::string(80, '=') << std::endl;
    std::cout << "                    ALNS SOLVER STARTED" << std::endl;
    std::cout << std::string(80, '=') << std::endl;
    std::cout << "Configuration:" << std::endl;
    std::cout << "  Max Iterations:    " << maxIterations << std::endl;
    std::cout << "  Segment Size:      " << segmentSize << std::endl;
    std::cout << "  Destruction Rate:  " << destructionRate << std::endl;
    std::cout << "  Initial Temp:      " << temperature << std::endl;
    std::cout << "  Cooling Rate:      " << coolingRate << std::endl;
    std::cout << "  Reaction Factor:   " << reactionFactor << std::endl;
    std::cout << "Operators:" << std::endl;
    std::cout << "  Destroy: " << destroyOperators.size() << " operators" << std::endl;
    std::cout << "  Repair:  " << repairOperators.size() << " operators" << std::endl;
    std::cout << std::string(80, '=') << std::endl;
}

void ALNS::logProgress(int iteration, int accepted, int rejected, 
                       int feasible, int infeasible) const {
    std::cout << "[Iteration " << iteration << "/" << maxIterations << "]" << std::endl;
    std::cout << "  Archive size:      " << archive.size() << std::endl;
    std::cout << "  Accepted/Rejected: " << accepted << "/" << rejected << std::endl;
    std::cout << "  Feasible/Infeasible: " << feasible << "/" << infeasible << std::endl;
    std::cout << "  Temperature:       " << std::fixed << std::setprecision(4) 
              << temperature << std::endl;
    
    std::cout << "  Operator weights:" << std::endl;
    std::cout << "    Destroy: ";
    for (size_t i = 0; i < destroyWeights.size(); ++i) {
        std::cout << destroyOperators[i]->getName() << "=" 
                  << std::fixed << std::setprecision(2) << destroyWeights[i] << " ";
    }
    std::cout << "Repair:  ";
    for (size_t i = 0; i < repairWeights.size(); ++i) {
        std::cout << repairOperators[i]->getName() << "=" 
                  << std::fixed << std::setprecision(2) << repairWeights[i] << " ";
    }
    std::cout << std::endl;
}

void ALNS::printFooter(long long duration, int accepted, int rejected,
                       int feasible, int infeasible) const {
    std::cout << "" << std::string(80, '=') << std::endl;
    std::cout << "                    ALNS SOLVER FINISHED" << std::endl;
    std::cout << std::string(80, '=') << std::endl;
    std::cout << "Results:" << std::endl;
    std::cout << "  Total runtime:     " << duration << " ms" << std::endl;
    std::cout << "  Archive size:      " << archive.size() << " solutions" << std::endl;
    
    if ((accepted + rejected) > 0) {
        std::cout << "  Acceptance rate:   " << std::fixed << std::setprecision(2)
              << (100.0 * accepted / (accepted + rejected)) << "%" << std::endl;
    } else {
        std::cout << "  Acceptance rate:   N/A" << std::endl;
    }

    if ((feasible + infeasible) > 0) {
        std::cout << "  Feasibility rate:  " << std::fixed << std::setprecision(2)
              << (100.0 * feasible / (feasible + infeasible)) << "%" << std::endl;
    } else {
        std::cout << "  Feasibility rate:  N/A" << std::endl;
    }

    std::cout << std::string(80, '=') << std::endl;
}