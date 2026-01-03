#include "../../include/alns/LocalSearch.h"
#include "../../include/core/Customer.h"
#include "../../include/core/Station.h"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <limits>

LocalSearch::LocalSearch(std::shared_ptr<Instance> inst) : instance(inst) {
    // Cache Station IDs
    for (const auto& node : instance->getNodes()) {
        if (node->getType() == NodeType::STATION) {
            stationIds.push_back(node->getId());
        }
    }
}

// ============================================================================
// MAIN LOOP
// ============================================================================
void LocalSearch::run(Solution& solution) {
    int maxIter = 50;
    int noImprove = 0;

    // Vòng lặp chính của Local Search
    for (int iter = 0; iter < maxIter; ++iter) {
        bool improved = false;

        // PHASE 1: Distance Optimization (Quan trọng nhất)
        // Tập trung giảm quãng đường -> giảm năng lượng gián tiếp
        if (runDistanceOptimization(solution)) {
            improved = true;
        }

        // PHASE 2: Charging Optimization
        // Thêm/Bớt/Thay thế trạm sạc
        if (runChargingOptimization(solution)) {
            improved = true;
        }

        // PHASE 3: Vehicle Reduction (Chỉ chạy định kỳ hoặc khi bế tắc)
        // (Thử làm rỗng các route nhỏ)
        if ((iter % 5 == 0 || !improved) && solution.getRoutes().size() > 1) {
            if (runVehicleReduction(solution)) {
                improved = true;
                // Nếu giảm được xe, reset bộ đếm để tối ưu lại các route mới
                noImprove = 0;
            }
        }

        if (improved) {
            solution.evaluateRoutes(); // Cập nhật lại toàn bộ thông số
            noImprove = 0;
        } else {
            noImprove++;
            if (noImprove > 3) break; // Early stopping nếu 3 vòng không cải thiện
        }
    }
}

// ============================================================================
// CORE: EVALUATE MOVE (DELTA ON COPY)
// ============================================================================
void LocalSearch::evaluateMove(const Solution& solution, MoveDescriptor& move, const LocalSearchWeights& weights) {
    move.eval.reset();
    move.eval.isFeasible = false;

    // 1. Tạo bản sao của các Route bị ảnh hưởng
    // Lưu ý: Route copy constructor chỉ copy vector<int>, rất nhẹ.
    const auto& routes = solution.getRoutes();
    Route r1 = routes[move.routeIdx1];
    double oldCost1 = r1.getTotalDistance() * weights.dist + r1.getTotalTime() * weights.time;
    double oldCost2 = 0.0;

    // Check nếu cần route thứ 2
    bool twoRoutes = (move.routeIdx1 != move.routeIdx2 && move.routeIdx2 >= 0);
    Route r2 = r1; // Placeholder
    if (twoRoutes) {
        r2 = routes[move.routeIdx2];
        oldCost2 = r2.getTotalDistance() * weights.dist + r2.getTotalTime() * weights.time;
    }

    try {
        // 2. Thực hiện hành động trên bản sao
        switch (move.type) {
            case MoveType::INTRA_RELOCATE:
            case MoveType::INTER_RELOCATE: {
                int nodeId = r1.getNodeAt(move.nodeIdx1);
                r1.removeNode(move.nodeIdx1);

                if (twoRoutes) {
                    r2.addNode(nodeId, move.nodeIdx2);
                } else {
                    // Intra-Relocate: Cần điều chỉnh index nếu chèn sau khi xóa
                    int targetIdx = move.nodeIdx2;
                    if (move.nodeIdx1 < move.nodeIdx2) targetIdx--;
                    r1.addNode(nodeId, targetIdx);
                }
                break;
            }
            case MoveType::INTER_SWAP: {
                // Giả định r1 != r2
                int n1 = r1.getNodeAt(move.nodeIdx1);
                int n2 = r2.getNodeAt(move.nodeIdx2);
                r1.removeNode(move.nodeIdx1); r1.addNode(n2, move.nodeIdx1);
                r2.removeNode(move.nodeIdx2); r2.addNode(n1, move.nodeIdx2);
                break;
            }
            case MoveType::INTRA_TWO_OPT:
                r1.reverseNodes(move.nodeIdx1, move.nodeIdx2);
                break;
            case MoveType::STATION_REMOVE:
                r1.removeNode(move.nodeIdx1);
                break;
            case MoveType::STATION_INSERT:
                r1.addNode(move.stationId, move.nodeIdx1);
                break;
            default:
                return;
        }

        // 3. Đánh giá lại (DP sạc pin chạy ở đây)
        r1.evaluate();
        if (twoRoutes) r2.evaluate();

        // 4. Kiểm tra khả thi
        if (!r1.isFeasible() || (twoRoutes && !r2.isFeasible())) {
            return; // Infeasible
        }

        // 5. Tính Delta
        double newDist = r1.getTotalDistance() + (twoRoutes ? r2.getTotalDistance() : 0);
        double oldDist = routes[move.routeIdx1].getTotalDistance() + (twoRoutes ? routes[move.routeIdx2].getTotalDistance() : 0);

        double newTime = r1.getTotalTime() + (twoRoutes ? r2.getTotalTime() : 0);
        double oldTime = routes[move.routeIdx1].getTotalTime() + (twoRoutes ? routes[move.routeIdx2].getTotalTime() : 0);

        double newEng = r1.getTotalChargeAmount() + (twoRoutes ? r2.getTotalChargeAmount() : 0);
        double oldEng = routes[move.routeIdx1].getTotalChargeAmount() + (twoRoutes ? routes[move.routeIdx2].getTotalChargeAmount() : 0);

        move.eval.isFeasible = true;
        move.eval.distanceDelta = newDist - oldDist;
        move.eval.timeDelta = newTime - oldTime;
        move.eval.energyDelta = newEng - oldEng;

        // Weighted Objective Delta
        move.eval.objectiveDelta = (move.eval.distanceDelta * weights.dist) +
                                   (move.eval.timeDelta * weights.time) +
                                   (move.eval.energyDelta * weights.energy);

    } catch (...) {
        move.eval.isFeasible = false;
    }
}

// ============================================================================
// APPLY MOVE
// ============================================================================
void LocalSearch::applyMove(Solution& solution, const MoveDescriptor& move) {
    auto& routes = solution.getRoutes();
    Route& r1 = routes[move.routeIdx1];

    if (move.type == MoveType::INTRA_RELOCATE) {
        int nodeId = r1.getNodeAt(move.nodeIdx1);
        r1.removeNode(move.nodeIdx1);
        int targetIdx = move.nodeIdx2;
        if (move.nodeIdx1 < move.nodeIdx2) targetIdx--;
        r1.addNode(nodeId, targetIdx);
        r1.evaluate();
    }
    else if (move.type == MoveType::INTER_RELOCATE) {
        Route& r2 = routes[move.routeIdx2];
        int nodeId = r1.getNodeAt(move.nodeIdx1);
        r1.removeNode(move.nodeIdx1);
        r2.addNode(nodeId, move.nodeIdx2);
        r1.evaluate(); r2.evaluate();
    }
    else if (move.type == MoveType::INTER_SWAP) {
        Route& r2 = routes[move.routeIdx2];
        int n1 = r1.getNodeAt(move.nodeIdx1);
        int n2 = r2.getNodeAt(move.nodeIdx2);
        r1.removeNode(move.nodeIdx1); r1.addNode(n2, move.nodeIdx1);
        r2.removeNode(move.nodeIdx2); r2.addNode(n1, move.nodeIdx2);
        r1.evaluate(); r2.evaluate();
    }
    else if (move.type == MoveType::INTRA_TWO_OPT) {
        r1.reverseNodes(move.nodeIdx1, move.nodeIdx2);
        r1.evaluate();
    }
    else if (move.type == MoveType::STATION_REMOVE) {
        r1.removeNode(move.nodeIdx1);
        r1.evaluate();
    }
    else if (move.type == MoveType::STATION_INSERT) {
        r1.addNode(move.stationId, move.nodeIdx1);
        r1.evaluate();
    }
}

// ============================================================================
// PHASE 1: DISTANCE OPTIMIZATION
// ============================================================================
bool LocalSearch::runDistanceOptimization(Solution& solution) {
    LocalSearchWeights weights;
    weights.dist = 1.0; weights.time = 0.001; weights.energy = 0.0; // Ưu tiên Distance

    bool improved = false;
    MoveDescriptor bestMove;

    // Chiến lược: First Improvement hoặc Best Improvement
    // Ở đây dùng Best Improvement trong từng neighborhood để chất lượng cao hơn

    if (searchRelocate(solution, bestMove, weights)) {
        applyMove(solution, bestMove);
        improved = true;
    }

    // Reset bestMove cho operator tiếp theo
    bestMove.reset();
    if (searchTwoOpt(solution, bestMove, weights)) {
        applyMove(solution, bestMove);
        improved = true;
    }

    bestMove.reset();
    if (searchSwap(solution, bestMove, weights)) {
        applyMove(solution, bestMove);
        improved = true;
    }

    return improved;
}

bool LocalSearch::searchRelocate(Solution& solution, MoveDescriptor& outBestMove, const LocalSearchWeights& weights) {
    outBestMove.reset();
    outBestMove.eval.objectiveDelta = -1e-5; // Phải cải thiện ít nhất epsilon
    bool found = false;

    int numRoutes = solution.getRoutes().size();

    for (int r1 = 0; r1 < numRoutes; ++r1) {
        // Heuristic: Bỏ qua route rỗng
        if (solution.getRoutes()[r1].size() <= 2) continue;

        // Chỉ xét việc di chuyển Customer, không di chuyển Station
        const auto& nodes1 = solution.getRoutes()[r1].getNodes();

        for (size_t i = 1; i < nodes1.size() - 1; ++i) {
            int nodeId = nodes1[i];
            if (instance->getNodeById(nodeId)->getType() != NodeType::CUSTOMER) continue;

            for (int r2 = 0; r2 < numRoutes; ++r2) {
                // Heuristic: Nếu r1 và r2 quá xa nhau -> bỏ qua (Trừ khi r1==r2)
                if (r1 != r2 && !areRoutesClose(solution.getRoutes()[r1], solution.getRoutes()[r2])) continue;

                const auto& nodes2 = solution.getRoutes()[r2].getNodes();
                for (size_t j = 1; j < nodes2.size(); ++j) {
                    if (r1 == r2 && (j == i || j == i + 1)) continue;

                    MoveDescriptor move;
                    move.type = (r1 == r2) ? MoveType::INTRA_RELOCATE : MoveType::INTER_RELOCATE;
                    move.routeIdx1 = r1; move.nodeIdx1 = i;
                    move.routeIdx2 = r2; move.nodeIdx2 = j;

                    evaluateMove(solution, move, weights);

                    if (move.eval.isFeasible && move.eval.objectiveDelta < outBestMove.eval.objectiveDelta) {
                        outBestMove = move;
                        found = true;
                    }
                }
            }
        }
    }
    return found;
}

bool LocalSearch::searchTwoOpt(Solution& solution, MoveDescriptor& outBestMove, const LocalSearchWeights& weights) {
    outBestMove.reset();
    outBestMove.eval.objectiveDelta = -1e-5;
    bool found = false;

    // Chỉ thực hiện Intra-Route 2-Opt
    for (int r = 0; r < solution.getRoutes().size(); ++r) {
        const auto& nodes = solution.getRoutes()[r].getNodes();
        if (nodes.size() < 4) continue;

        for (size_t i = 1; i < nodes.size() - 2; ++i) {
            for (size_t j = i + 1; j < nodes.size() - 1; ++j) {
                MoveDescriptor move;
                move.type = MoveType::INTRA_TWO_OPT;
                move.routeIdx1 = r;
                move.nodeIdx1 = i;
                move.nodeIdx2 = j;

                evaluateMove(solution, move, weights);

                if (move.eval.isFeasible && move.eval.objectiveDelta < outBestMove.eval.objectiveDelta) {
                    outBestMove = move;
                    found = true;
                }
            }
        }
    }
    return found;
}

bool LocalSearch::searchSwap(Solution& solution, MoveDescriptor& outBestMove, const LocalSearchWeights& weights) {
    // Tương tự Relocate nhưng là SWAP 2 khách hàng giữa 2 route khác nhau
    // (Bạn tự triển khai logic loop tương tự searchRelocate)
    return false;
}


// ============================================================================
// PHASE 2: CHARGING OPTIMIZATION
// ============================================================================
bool LocalSearch::runChargingOptimization(Solution& solution) {
    LocalSearchWeights weights;
    weights.dist = 0.1; weights.energy = 1.0; weights.time = 0.0; // Ưu tiên Energy

    MoveDescriptor bestMove;
    bool improved = false;

    // 1. Thử xóa các trạm sạc dư thừa
    if (searchStationRemoval(solution, bestMove, weights)) {
        applyMove(solution, bestMove);
        improved = true;
    }

    // 2. Thử chèn trạm sạc vào các vị trí cần thiết (nếu đang Infeasible về năng lượng)
    // Hoặc chèn để giảm thời gian chờ
    // (Logic này phức tạp hơn, có thể để sau)

    return improved;
}

bool LocalSearch::searchStationRemoval(Solution& solution, MoveDescriptor& outBestMove, const LocalSearchWeights& weights) {
    outBestMove.reset();
    outBestMove.eval.objectiveDelta = -1e-5;
    bool found = false;

    for (int r = 0; r < solution.getRoutes().size(); ++r) {
        const auto& nodes = solution.getRoutes()[r].getNodes();
        for (size_t i = 1; i < nodes.size() - 1; ++i) {
            int nodeId = nodes[i];
            // Nếu là trạm sạc
            if (instance->getNodeById(nodeId)->getType() == NodeType::STATION) {
                MoveDescriptor move;
                move.type = MoveType::STATION_REMOVE;
                move.routeIdx1 = r;
                move.nodeIdx1 = i;

                evaluateMove(solution, move, weights);

                // Nếu xóa đi mà vẫn Feasible và giảm cost (Energy/Distance)
                if (move.eval.isFeasible && move.eval.objectiveDelta < outBestMove.eval.objectiveDelta) {
                    outBestMove = move;
                    found = true;
                }
            }
        }
    }
    return found;
}

// ============================================================================
// PHASE 4: VEHICLE REDUCTION (Greedy)
// ============================================================================
bool LocalSearch::runVehicleReduction(Solution& solution) {
    // Chiến thuật: Tìm route ngắn nhất (ít khách nhất), cố gắng relocate hết khách đi

    int targetRouteIdx = -1;
    size_t minCust = 9999;

    for (int r = 0; r < solution.getRoutes().size(); ++r) {
        size_t count = 0;
        for (int nodeId : solution.getRoutes()[r].getNodes()) {
            if (instance->getNodeById(nodeId)->getType() == NodeType::CUSTOMER) count++;
        }
        if (count > 0 && count < minCust) {
            minCust = count;
            targetRouteIdx = r;
        }
    }

    if (targetRouteIdx == -1) return false;

    // Thử di chuyển từng khách
    Solution tempSol = solution;
    Route& targetRoute = tempSol.getRoutes()[targetRouteIdx];
    std::vector<int> nodesToMove = targetRoute.getNodes();

    // Xóa route mục tiêu khỏi giải pháp tạm
    tempSol.removeRoute(targetRouteIdx);

    LocalSearchWeights weights; // Mặc định

    bool allMoved = true;
    for (int nodeId : nodesToMove) {
        if (instance->getNodeById(nodeId)->getType() != NodeType::CUSTOMER) continue;

        // Tìm vị trí chèn tốt nhất trong các route còn lại
        double bestInsertCost = std::numeric_limits<double>::infinity();
        int bestR = -1, bestP = -1;

        for (int r = 0; r < tempSol.getRoutes().size(); ++r) {
            Route& candidateRoute = tempSol.getRoutes()[r];
            for (size_t p = 1; p < candidateRoute.getNodes().size(); ++p) {
                // Dùng hàm checkInsertionCost của Route (O(N) hoặc O(1) tùy cài đặt)
                auto res = candidateRoute.checkInsertionCost(nodeId, p);
                if (res.isFeasible && res.deltaDistance < bestInsertCost) {
                    bestInsertCost = res.deltaDistance;
                    bestR = r;
                    bestP = p;
                }
            }
        }

        if (bestR != -1) {
            tempSol.getRoutes()[bestR].addNode(nodeId, bestP);
            // Cập nhật lại logic DP cho route vừa chèn để chuẩn bị cho khách tiếp theo
            tempSol.getRoutes()[bestR].evaluate();
        } else {
            allMoved = false;
            break;
        }
    }

    if (allMoved) {
        solution = tempSol; // Thành công giảm 1 xe
        return true;
    }

    return false;
}

// ============================================================================
// HEURISTICS HELPERS
// ============================================================================
LocalSearch::RouteCentroid LocalSearch::computeCentroid(const Route& route) const {
    double sumX = 0, sumY = 0;
    int count = 0;
    for (int nodeId : route.getNodes()) {
        auto node = instance->getNodeById(nodeId);
        if (node->getType() == NodeType::CUSTOMER) {
            sumX += node->getX();
            sumY += node->getY();
            count++;
        }
    }
    if (count == 0) return {0, 0}; // Nên return tọa độ depot
    return {sumX / count, sumY / count};
}

bool LocalSearch::areRoutesClose(const Route& r1, const Route& r2, double threshold) const {
    auto c1 = computeCentroid(r1);
    auto c2 = computeCentroid(r2);
    double dist = std::sqrt(std::pow(c1.x - c2.x, 2) + std::pow(c1.y - c2.y, 2));
    return dist < threshold;
    // Lưu ý: threshold này nên tính động dựa trên kích thước bản đồ,
    // nhưng hardcode tạm 50.0 cho Solomon map (thường 100x100).
}