#include "../include/ALNS.h"
#include "../include/Instance.h"
#include "../include/Customer.h"
#include "../include/Station.h"
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
    maxIterations = 500;
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

                // Lặp qua mọi khách hàng trong route_i để thử di dời
                // Lưu ý: lặp ngược để việc xóa theo chỉ số không ảnh hưởng đến các phần tử chưa xét
                for (int c_idx = route_i.getInfos().size() - 2; c_idx >= 1; --c_idx) {
                    
                    auto customerToMove = std::dynamic_pointer_cast<Customer>(route_i.getInfos()[c_idx].node);
                    if (!customerToMove) continue; // Bỏ qua nếu không phải là khách hàng

                    // --- SỬA LỖI: TÍNH TOÁN CHI PHÍ TIẾT KIỆM (COST SAVING) MỘT CÁCH CHÍNH XÁC ---
                    // Lấy các điểm liền trước và liền sau của khách hàng trong route_i
                    auto prev_node = route_i.getInfos()[c_idx - 1].node;
                    auto next_node = route_i.getInfos()[c_idx + 1].node;

                    // Chi phí tiết kiệm = (thời gian từ trước -> hiện tại + thời gian từ hiện tại -> sau) - (thời gian từ trước -> sau)
                    double time_saving = (instance->getTime(prev_node->getId(), customerToMove->getId()) +
                                        instance->getTime(customerToMove->getId(), next_node->getId())) -
                                       instance->getTime(prev_node->getId(), next_node->getId());

                    int bestNewPos = -1;
                    double bestTotalDelta = std::numeric_limits<double>::max();

                    // --- Tìm vị trí chèn tốt nhất trong route_j ---
                    for (int pos_j = 1; pos_j < route_j.getInfos().size(); ++pos_j) {
                        // Sử dụng hàm evaluateInsertion để đánh giá việc chèn
                        EvaluationResult result = route_j.evaluateInsertion(customerToMove, pos_j);
                        
                        if (result.isFeasible) {
                            // result.costDelta là chi phí tăng thêm khi chèn vào route_j
                            double totalDelta = result.costDelta - time_saving;

                            if (totalDelta < bestTotalDelta) {
                                bestTotalDelta = totalDelta;
                                bestNewPos = pos_j;
                            }
                        }
                    }

                    // --- Thực hiện di dời nếu có lợi (delta tổng < 0) ---
                    if (bestNewPos != -1 && bestTotalDelta < -1e-6) { // Sử dụng sai số nhỏ
                        
                        // Thực hiện thay đổi trên các tuyến đường thực tế
                        route_i.remove(c_idx);
                        route_j.insert(customerToMove, bestNewPos);

                        improvement = true; // Báo hiệu đã có cải thiện
                        
                        // Thoát và bắt đầu lại Local Search từ đầu để đảm bảo tính nhất quán
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
// MAIN SOLVE METHOD - Cốt lõi của thuật toán ALNS
// ============================================================================
std::vector<Solution> ALNS::solve() {
    // --- KHỞI TẠO ---
    if (!instance) {
        throw std::runtime_error("Instance is null in solve()");
    }

    // Bắt đầu đếm thời gian thực thi
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // In thông tin cấu hình của thuật toán
    printHeader();
    
    // --- TẠO GIẢI PHÁP BAN ĐẦU ---
    // Tạo ra một giải pháp ban đầu (thường là một giải pháp đơn giản nhưng hợp lệ)
    Solution currentSolution = generateInitialSolution();
    // Tính toán các giá trị mục tiêu cho giải pháp ban đầu
    calculateObjectives(currentSolution);
    // Cập nhật kho lưu trữ Pareto (archive) với giải pháp ban đầu này
    updateArchive(currentSolution);
    
    // `bestSolution` dùng để theo dõi giải pháp tốt nhất tìm thấy (có thể không cần thiết trong đa mục tiêu, nhưng hữu ích cho so sánh)
    Solution bestSolution = currentSolution;
    
    // --- KHỞI TẠO CÁC BIẾN THỐNG KÊ VÀ ĐIỀU KHIỂN ---
    int iterationsSinceImprovement = 0; // Đếm số vòng lặp kể từ lần cải thiện cuối cùng (dùng cho reheating)
    int acceptedSolutions = 0;          // Số giải pháp mới được chấp nhận
    int rejectedSolutions = 0;          // Số giải pháp mới bị từ chối
    int totalFeasible = 0;              // Tổng số giải pháp hợp lệ được tạo ra
    int totalInfeasible = 0;            // Tổng số giải pháp không hợp lệ được tạo ra

    // ========================================================
    // --- VÒNG LẶP CHÍNH CỦA ALNS ---
    // ========================================================
    for (int iteration = 0; iteration < maxIterations; ++iteration) {
        // Tạo một bản sao của giải pháp hiện tại để thực hiện các thay đổi
        Solution tempSolution = currentSolution;

        // --- BƯỚC 1: CHỌN TOÁN TỬ PHÁ HỦY VÀ SỬA CHỮA ---
        // Sử dụng phương pháp Roulette-wheel để chọn toán tử dựa trên trọng số của chúng
        auto destroyPair = selectOperator(destroyOperators, destroyWeights);
        IDestroyOperator* destroyOp = destroyPair.first; // Toán tử phá hủy được chọn
        size_t destroyIdx = destroyPair.second;          // Chỉ số của toán tử

        auto repairPair = selectOperator(repairOperators, repairWeights);
        IRepairOperator* repairOp = repairPair.first;   // Toán tử sửa chữa được chọn
        size_t repairIdx = repairPair.second;           // Chỉ số của toán tử
        
        // Tăng bộ đếm số lần sử dụng cho các toán tử được chọn
        destroyUses[destroyIdx]++;
        repairUses[repairIdx]++;

        // --- BƯỚC 2: PHÁ HỦY VÀ SỬA CHỮA GIẢI PHÁP ---
        // Áp dụng toán tử phá hủy để loại bỏ một số khách hàng khỏi giải pháp
        auto removedCustomers = destroyOp->destroy(tempSolution, *instance, destructionRate, rng);

        // --- DEBUG ---
        std::cout << "[DEBUG] ALNS::solve - Destroyed " << removedCustomers.size() << " customers: { ";
        for(const auto& c : removedCustomers) {
            std::cout << "C" << c->getId() << " ";
        }
        std::cout << "}" << std::endl;
        // --- END DEBUG ---

        // Áp dụng toán tử sửa chữa để chèn lại các khách hàng đã bị loại bỏ
        bool repaired = repairOp->repair(tempSolution, *instance, removedCustomers, rng);

        // --- BƯỚC 3: (TÙY CHỌN) ÁP DỤNG TÌM KIẾM ĐỊA PHƯƠNG (LOCAL SEARCH) ---
        // Nếu giải pháp được sửa chữa thành công, áp dụng LS để cải thiện thêm
        // if (repaired) {
        //     applyLocalSearch(tempSolution);
        // }

        // --- BƯỚC 4: ĐÁNH GIÁ VÀ CHẤP NHẬN GIẢI PHÁP MỚI ---
        // Chỉ xem xét nếu giải pháp mới là hợp lệ
        if (repaired && isFeasible(tempSolution)) {
            totalFeasible++;
            // Tính toán các giá trị mục tiêu cho giải pháp mới
            calculateObjectives(tempSolution);

            // So sánh giải pháp mới (`tempSolution`) với giải pháp hiện tại (`currentSolution`)
            Dominance status = dominanceCheck(tempSolution, currentSolution);
            // Cập nhật kho lưu trữ Pareto với giải pháp mới (nếu nó không bị trội)
            bool addedToArchive = updateArchive(tempSolution);

            // --- CẬP NHẬT ĐIỂM THƯỞNG CHO TOÁN TỬ ---
            int reward = 0;
            if (addedToArchive) { // Nếu giải pháp mới cải thiện được kho Pareto
                reward = sigma1; // Thưởng cao nhất
                iterationsSinceImprovement = 0; // Reset bộ đếm vì đã có cải thiện
            } else if (status == DOMINATES) { // Nếu giải pháp mới trội hơn giải pháp hiện tại
                reward = sigma2; // Thưởng trung bình
                iterationsSinceImprovement = 0;
            } else if (status != DOMINATED) { // Nếu giải pháp mới không bị trội (non-dominated)
                reward = sigma3; // Thưởng thấp
            }

            // Cộng điểm thưởng cho các toán tử đã được sử dụng
            if (reward > 0) {
                destroyScores[destroyIdx] += reward;
                repairScores[repairIdx] += reward;
            }

            // --- TIÊU CHUẨN CHẤP NHẬN (SIMULATED ANNEALING) ---
            bool accepted = false;
            if (status == DOMINATES) { // Luôn chấp nhận giải pháp tốt hơn
                currentSolution = tempSolution;
                accepted = true;
            } else if (status != DOMINATED) { // Nếu giải pháp mới không bị trội
                // Chấp nhận giải pháp kém hơn với một xác suất nhất định (Simulated Annealing)
                double acceptProb = std::exp(-1.0 / temperature); // Xác suất phụ thuộc vào "nhiệt độ"
                std::uniform_real_distribution<> dist(0.0, 1.0);
                if (dist(rng) < acceptProb) {
                    currentSolution = tempSolution;
                    accepted = true;
                }
            }

            if (accepted) acceptedSolutions++; else rejectedSolutions++;
        } else {
            totalInfeasible++; // Tăng bộ đếm giải pháp không hợp lệ
        }

        // --- BƯỚC 5: CẬP NHẬT TRỌNG SỐ VÀ NHIỆT ĐỘ ---
        // Cập nhật trọng số của các toán tử sau mỗi `segmentSize` vòng lặp
        if (iteration > 0 && iteration % segmentSize == 0) {
            updateWeights();
            
            // In ra tiến trình sau mỗi 5 segments
            if (iteration % (segmentSize * 5) == 0) {
                logProgress(iteration, acceptedSolutions, rejectedSolutions, 
                           totalFeasible, totalInfeasible);
                std::cout << currentSolution.toString();
            }
        }

        // Giảm nhiệt độ theo tỷ lệ `coolingRate` (làm nguội)
        temperature *= coolingRate;
        if (temperature < minimumTemperature) {
            temperature = minimumTemperature;
        }
        
        // Chiến lược "Reheating": Nếu không có cải thiện trong một thời gian dài, tăng lại nhiệt độ
        if (iterationsSinceImprovement > maxIterations / 4) {
            temperature = initialTemperature * 0.5; // Reset nhiệt độ về một mức cao hơn
            iterationsSinceImprovement = 0; // Reset bộ đếm
            std::cout << "  [Reheating] Temperature reset to " << temperature << std::endl;
        }
        
        iterationsSinceImprovement++;
    }

    // --- KẾT THÚC ---
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    // In báo cáo tổng kết
    printFooter(duration.count(), acceptedSolutions, rejectedSolutions, 
                totalFeasible, totalInfeasible);
    
    // Trả về tập hợp các giải pháp không bị trội (Pareto front)
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
// Helper function to find the nearest station to a given node
std::shared_ptr<Station> findNearestStation(const std::shared_ptr<Node>& fromNode, const std::vector<std::shared_ptr<Station>>& stations, const std::shared_ptr<const Instance>& instance) {
    if (stations.empty()) {
        return nullptr;
    }
    double min_dist = std::numeric_limits<double>::max();
    std::shared_ptr<Station> nearest_station = nullptr;
    for (const auto& station : stations) {
        double dist = instance->getDistance(fromNode->getId(), station->getId());
        if (dist < min_dist) {
            min_dist = dist;
            nearest_station = station;
        }
    }
    return nearest_station;
}


Solution ALNS::generateInitialSolution() {
    std::cout << "Generating initial solution using Sweep Algorithm with station insertion logic..." << std::endl;
    Solution sol;

    // 1. Phân loại các nút
    std::shared_ptr<Depot> depot;
    std::vector<std::shared_ptr<Customer>> customers;
    std::vector<std::shared_ptr<Station>> stations;

    for (const auto& node : instance->getNodes()) {
        if (auto d = std::dynamic_pointer_cast<Depot>(node)) {
            depot = d;
        } else if (auto c = std::dynamic_pointer_cast<Customer>(node)) {
            customers.push_back(c);
        } else if (auto s = std::dynamic_pointer_cast<Station>(node)) {
            stations.push_back(s);
        }
    }

    if (!depot) {
        throw std::runtime_error("Depot not found in instance!");
    }

    // 2. Thuật toán Sweep: Sắp xếp khách hàng theo góc
    std::sort(customers.begin(), customers.end(), [&](const auto& a, const auto& b) {
        return std::atan2(a->getY() - depot->getY(), a->getX() - depot->getX()) <
               std::atan2(b->getY() - depot->getY(), b->getX() - depot->getX());
    });

    int routeId = 0;

    auto create_new_route = [&]() {
        auto vehicle = std::make_shared<Vehicle>(routeId, instance->getVehicleCapacity(), instance->getVehicleBattery(), instance->getVehicleEnergyRate());
        Route newRoute(routeId++, vehicle, instance);
        newRoute.insert(depot, 0);
        newRoute.insert(depot, 1);
        return newRoute;
    };

    sol.routes.push_back(create_new_route());

    for (const auto& customer : customers) {
        bool inserted = false;
        int best_route_idx = -1;
        int best_pos = -1;
        double min_cost_delta = std::numeric_limits<double>::max();
        bool requires_station = false;
        std::shared_ptr<Station> station_for_plan_b = nullptr;
        int station_pos = -1;

        for (int i = 0; i < sol.routes.size(); ++i) {
            Route& route = sol.routes[i];
            for (int pos = 1; pos < route.getInfos().size(); ++pos) {
                // Plan A: Direct insertion
                EvaluationResult direct_result = route.evaluateInsertion(customer, pos);
                if (direct_result.isFeasible && direct_result.costDelta < min_cost_delta) {
                    min_cost_delta = direct_result.costDelta;
                    best_route_idx = i;
                    best_pos = pos;
                    requires_station = false;
                } else if (!direct_result.isFeasible) {
                    // Plan B: Try inserting a station
                    auto node_before = route.getInfos()[pos - 1].node;
                    auto station_to_try = findNearestStation(node_before, stations, instance);
                    if (station_to_try) {
                        Route tempRoute = route;
                        EvaluationResult station_result = tempRoute.evaluateInsertion(station_to_try, pos);
                        if (station_result.isFeasible) {
                            tempRoute.insert(station_to_try, pos);
                            EvaluationResult customer_result = tempRoute.evaluateInsertion(customer, pos + 1);
                            if (customer_result.isFeasible) {
                                double combined_cost = station_result.costDelta + customer_result.costDelta;
                                if (combined_cost < min_cost_delta) {
                                    min_cost_delta = combined_cost;
                                    best_route_idx = i;
                                    best_pos = pos + 1;
                                    requires_station = true;
                                    station_for_plan_b = station_to_try;
                                    station_pos = pos;
                                }
                            }
                        }
                    }
                }
            }
        }

        if (best_route_idx != -1) {
            if (requires_station) {
                sol.routes[best_route_idx].insert(station_for_plan_b, station_pos);
                sol.routes[best_route_idx].insert(customer, best_pos);
            } else {
                sol.routes[best_route_idx].insert(customer, best_pos);
            }
            inserted = true;
        }

        if (!inserted) {
            // Fallback: create a new route for this customer
            Route newRoute = create_new_route();
            // Try to insert with a station if needed
            EvaluationResult direct_result = newRoute.evaluateInsertion(customer, 1);
            if (direct_result.isFeasible) {
                newRoute.insert(customer, 1);
            } else {
                auto station_to_try = findNearestStation(depot, stations, instance);
                if (station_to_try) {
                    newRoute.insert(station_to_try, 1);
                    newRoute.insert(customer, 2);
                }
            }
            sol.routes.push_back(newRoute);
        }
    }
    
    // Clean up empty routes
    sol.routes.erase(
        std::remove_if(sol.routes.begin(), sol.routes.end(),
            [](const Route& r) { return r.getCustomerCount() == 0; }),
        sol.routes.end()
    );


    std::cout << "Initial solution generated with " << sol.routes.size() << " routes." << std::endl;
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
// FEASIBILITY CHECK - Comprehensive validation
// ============================================================================
// bool ALNS::isFeasible(const Solution& solution) {
//     if (solution.routes.empty()) {
//         return true; // Một giải pháp không có tuyến nào (nếu không có khách hàng) là hợp lệ
//     }
    
//     constexpr double EPSILON = 1e-4;
//     std::set<int> customers_served; // Dùng để kiểm tra tính duy nhất
//     int total_customers_in_solution = 0;

//     for (const auto& route : solution.routes) {
//         const auto& infos = route.getInfos();
        
//         // --- Vấn đề 1 & 4: Kiểm tra cấu trúc tuyến ---
//         if (infos.empty()) {
//             continue; // Bỏ qua tuyến rỗng (nếu Local Search tạo ra)
//         }
//         if (infos.size() < 2) {
//              return false; // Tuyến phải có ít nhất [Depot, Depot]
//         }
//         if (!std::dynamic_pointer_cast<Depot>(infos.front().node) ||
//             !std::dynamic_pointer_cast<Depot>(infos.back().node)) {
//             return false; // Phải bắt đầu và kết thúc tại Depot
//         }

//         auto vehicle = route.getVehicle();
//         const double capacity = vehicle->getCapacity();
//         const double batteryCapacity = vehicle->getBatteryCapacity();

//         // --- Kiểm tra tính nhất quán của từng điểm dừng ---
//         // (Giả định rằng `checkAndUpdateInfos` đã làm đúng)
//         // (Chúng ta chỉ cần kiểm tra lại các giá trị đã tính)
//         for (const auto& info : infos) {
            
//             // Battery constraints
//             if (info.arrival_battery < -EPSILON || 
//                 info.departure_battery > batteryCapacity + EPSILON) {
//                 return false; // Pin vi phạm
//             }

//             // Load constraints
//             if (info.departure_load < -EPSILON || 
//                 info.departure_load > capacity + EPSILON) {
//                 return false; // Tải trọng vi phạm
//             }

//             // Time window constraints
//             if (auto customer = std::dynamic_pointer_cast<const Customer>(info.node)) {
//                 // Vấn đề 2: Check DueDate
//                 if (info.arrival_time > customer->getDueDate() + EPSILON) {
//                     return false; // Trễ giờ
//                 }
                
//                 // GHI CHÚ: Không cần check ReadyTime (arrival < readyTime)
//                 // vì `recalculateInfos` đã xử lý bằng cách thêm wait_time.
                
//                 // Kiểm tra tính duy nhất
//                 if (customers_served.count(customer->getId())) {
//                     return false; // Khách hàng này đã được phục vụ ở tuyến khác
//                 }
//                 customers_served.insert(customer->getId());
//                 total_customers_in_solution++;

//             } 
//         }
        
//         // ** LOẠI BỎ LỖI: Không kiểm tra tải trọng về 0 **
//         // if (std::abs(infos.back().departure_load) > EPSILON) {
//         //     return false;
//         // }
//     }

//     // --- Kiểm tra tính toàn vẹn của Solution ---
//     // Đảm bảo mọi khách hàng trong 'instance' đều được phục vụ
//     int total_customers_in_instance = 0;
//     for (const auto& node : instance->getNodes()) {
//         if (std::dynamic_pointer_cast<Customer>(node)) {
//             total_customers_in_instance++;
//         }
//     }
    
//     if (total_customers_in_solution != total_customers_in_instance) {
//         return false; // Số lượng khách hàng phục vụ không khớp với bài toán
//     }

//     return true;
// }

bool ALNS::isFeasible(const Solution& solution) {
    if (solution.routes.empty()) {
        std::cout << "[DEBUG isFeasible] No routes, considered feasible." << std::endl; // Debug
        return true; // Một giải pháp không có tuyến nào (nếu không có khách hàng) là hợp lệ
    }

    constexpr double EPSILON = 1e-4;
    std::set<int> customers_served; // Dùng để kiểm tra tính duy nhất
    int total_customers_in_solution = 0;

    for (const auto& route : solution.routes) {
        const auto& infos = route.getInfos();

        // --- Vấn đề 1 & 4: Kiểm tra cấu trúc tuyến ---
        if (infos.empty()) {
            // Tuyến rỗng nhưng không phải giải pháp rỗng thì không hợp lệ trừ khi local search tạo ra
            // Tạm thời bỏ qua nếu LS có thể tạo tuyến rỗng rồi xóa sau
            // std::cout << "[DEBUG isFeasible] Route " << route.getId() << " is empty (but solution is not)." << std::endl;
            continue; // Bỏ qua tuyến rỗng (nếu Local Search tạo ra)
        }
        if (infos.size() < 2) {
             std::cout << "[DEBUG isFeasible] Route " << route.getId() << " has less than 2 nodes. Size=" << infos.size() << std::endl;
             return false; // Tuyến phải có ít nhất [Depot, Depot]
        }
        // Kiểm tra node đầu cuối có phải là Depot không
        auto start_node_ptr = infos.front().node;
        auto end_node_ptr = infos.back().node;
        if (!start_node_ptr || !end_node_ptr ||
            !std::dynamic_pointer_cast<Depot>(start_node_ptr) ||
            !std::dynamic_pointer_cast<Depot>(end_node_ptr)) {
            std::cout << "[DEBUG isFeasible] Route " << route.getId() << " does not start/end with Depot."
                      << " Start: " << (start_node_ptr ? start_node_ptr->getId() : -1) << " Type: " << (start_node_ptr ? typeid(*start_node_ptr).name() : "null")
                      << ", End: " << (end_node_ptr ? end_node_ptr->getId() : -1) << " Type: " << (end_node_ptr ? typeid(*end_node_ptr).name() : "null") << std::endl;
            return false; // Phải bắt đầu và kết thúc tại Depot
        }


        auto vehicle = route.getVehicle();
        if (!vehicle) {
             std::cout << "[DEBUG isFeasible] Route " << route.getId() << " has null vehicle pointer." << std::endl;
             return false; // Lỗi logic, route phải có vehicle
        }
        const double capacity = vehicle->getCapacity();
        const double batteryCapacity = vehicle->getBatteryCapacity();

        // --- Kiểm tra tính nhất quán của từng điểm dừng ---
        for (size_t i = 0; i < infos.size(); ++i) { // Use index for better debugging
            const auto& info = infos[i];
            const auto& node = info.node; // Lấy node ra để kiểm tra null

             if (!node) {
                  std::cout << "[DEBUG isFeasible] Null node pointer encountered in route " << route.getId() << " at index " << i << std::endl;
                  return false; // Lỗi logic
             }


            // Battery constraints
            if (info.arrival_battery < -EPSILON ||
                info.departure_battery > batteryCapacity + EPSILON) {
                 std::cout << "[DEBUG isFeasible] Battery violation on route " << route.getId() << " at node " << i << " (ID: " << node->getId() << ")"
                           << ": arrival=" << info.arrival_battery << ", departure=" << info.departure_battery << ", capacity=" << batteryCapacity << std::endl;
                return false; // Pin vi phạm
            }

            // Load constraints
            // Check absolute load bounds
             if (info.arrival_load < -EPSILON || info.arrival_load > capacity + EPSILON) {
                 std::cout << "[DEBUG isFeasible] Arrival Load violation on route " << route.getId() << " at node " << i << " (ID: " << node->getId() << ")"
                           << ": arrival_load=" << info.arrival_load << ", capacity=" << capacity << std::endl;
                 return false;
             }
            if (info.departure_load < -EPSILON ||
                info.departure_load > capacity + EPSILON) {
                  std::cout << "[DEBUG isFeasible] Departure Load violation on route " << route.getId() << " at node " << i << " (ID: " << node->getId() << ")"
                           << ": departure_load=" << info.departure_load << ", capacity=" << capacity << std::endl;
                 return false; // Tải trọng vi phạm
            }
             // Check load drop only happens at customers
             if (i > 0 && info.arrival_load < infos[i-1].departure_load - EPSILON) {
                  std::cout << "[DEBUG isFeasible] Load decreased unexpectedly between node " << infos[i-1].node->getId() << " and " << node->getId() << std::endl;
                 return false;
             }
             if (auto cust_check = std::dynamic_pointer_cast<const Customer>(node)) {
                 if (std::abs(info.arrival_load - info.departure_load - cust_check->getDemand()) > EPSILON) {
                     std::cout << "[DEBUG isFeasible] Load drop mismatch at Customer " << node->getId() << ": arrival=" << info.arrival_load << ", departure=" << info.departure_load << ", demand=" << cust_check->getDemand() << std::endl;
                     return false;
                 }
             } else { // Stations and Depots
                 // Cho phép load thay đổi ở depot đầu tiên (khởi tạo)
                 if (i > 0 && std::abs(info.arrival_load - info.departure_load) > EPSILON) {
                     std::cout << "[DEBUG isFeasible] Load changed unexpectedly at non-customer node " << node->getId() << ": arrival=" << info.arrival_load << ", departure=" << info.departure_load << std::endl;
                     return false;
                 }
             }


            // Time window constraints
            if (auto customer = std::dynamic_pointer_cast<const Customer>(node)) {
                // Vấn đề 2: Check DueDate
                if (info.arrival_time > customer->getDueDate() + EPSILON) {
                     std::cout << "[DEBUG isFeasible] DueDate violation on route " << route.getId() << " at customer " << i << " (ID: " << node->getId() << ")"
                               << ": arrival_time=" << info.arrival_time << ", dueDate=" << customer->getDueDate() << std::endl;
                    return false; // Trễ giờ
                }
                 // Check ReadyTime implicitly (start service time should be >= ready time)
                 double start_service = info.arrival_time + std::max(0.0, customer->getReadyTime() - info.arrival_time);
                 if (start_service < customer->getReadyTime() - EPSILON) {
                      std::cout << "[DEBUG isFeasible] ReadyTime violation (logic error?) on route " << route.getId() << " at customer " << i << " (ID: " << node->getId() << ")"
                                << ": start_service=" << start_service << ", readyTime=" << customer->getReadyTime() << std::endl;
                     return false;
                 }


                // Kiểm tra tính duy nhất
                if (customers_served.count(customer->getId())) {
                     std::cout << "[DEBUG isFeasible] Customer " << customer->getId() << " served multiple times!" << std::endl;
                    return false; // Khách hàng này đã được phục vụ ở tuyến khác
                }
                customers_served.insert(customer->getId());
                total_customers_in_solution++;

            }
             else if (auto depot = std::dynamic_pointer_cast<const Depot>(node)) {
                 // Check depot end time only for the last node
                 if (i == infos.size() - 1 && info.arrival_time > depot->getLastTime() + EPSILON) {
                      std::cout << "[DEBUG isFeasible] Depot end time violation on route " << route.getId()
                                << ": arrival_time=" << info.arrival_time << ", lastTime=" << depot->getLastTime() << std::endl;
                     return false;
                 }
            }
             else if (std::dynamic_pointer_cast<Station>(node)) {
                 // Có thể thêm kiểm tra ràng buộc thời gian cho Station nếu cần
             } else {
                  std::cout << "[DEBUG isFeasible] Unknown node type encountered in route " << route.getId() << " at index " << i << " (ID: " << node->getId() << ")" << std::endl;
                  return false; // Lỗi logic
             }
        } // End for loop through infos

        // Kiểm tra tải trọng cuối cùng tại depot cuối cùng phải bằng 0
         if (infos.size() > 0) { // Check if route is not empty
             const auto& last_info = infos.back();
             if (std::abs(last_info.departure_load) > EPSILON) {
                 std::cout << "[DEBUG isFeasible] Final load at end depot of route " << route.getId() << " is not zero: " << last_info.departure_load << std::endl;
                 return false;
             }
         }

    } // End for loop through routes

    // --- Kiểm tra tính toàn vẹn của Solution ---
    // Đảm bảo mọi khách hàng trong 'instance' đều được phục vụ
    int total_customers_in_instance = 0;
    if (instance) { // Check if instance is valid
        for (const auto& node : instance->getNodes()) {
            if (std::dynamic_pointer_cast<Customer>(node)) {
                total_customers_in_instance++;
            }
        }
    } else {
        std::cerr << "[ERROR isFeasible] Instance pointer is null!" << std::endl;
        return false; // Cannot verify customer count without instance
    }


    if (total_customers_in_solution != total_customers_in_instance) {
         std::cout << "[DEBUG isFeasible] Customer count mismatch: served=" << total_customers_in_solution
                   << ", instance=" << total_customers_in_instance << std::endl;
        return false; // Số lượng khách hàng phục vụ không khớp với bài toán
    }

    // std::cout << "[DEBUG isFeasible] Solution is FEASIBLE." << std::endl; // Optional Debug
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

#include <sstream>
#include "../include/Station.h"

std::string Solution::toString() const {
    std::stringstream ss;
    ss << "Solution with " << routes.size() << " routes:\n";
    for (const auto& route : routes) {
        ss << "  Route " << route.getId() << ": ";
        const auto& infos = route.getInfos();
        for (size_t i = 0; i < infos.size(); ++i) {
            const auto& node = infos[i].node;
            if (dynamic_cast<const Depot*>(node.get())) {
                ss << "Depot(" << node->getId() << ")";
            } else if (auto c = dynamic_cast<const Customer*>(node.get())) {
                ss << "Customer(" << c->getId() << ")";
            } else if (auto s = dynamic_cast<const Station*>(node.get())) {
                ss << "Station(" << s->getId() << ")";
            }
            if (i < infos.size() - 1) {
                ss << " -> ";
            }
        }
        ss << "\n";
    }
    return ss.str();
}