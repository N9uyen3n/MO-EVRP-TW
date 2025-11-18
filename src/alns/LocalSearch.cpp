// src/alns/LocalSearch.cpp (Đã sửa lỗi evaluateMove)

#include "alns/LocalSearch.h"
#include "core/Station.h"
#include <limits>
#include <iostream>
#include <algorithm>
#include <utility>

// Trọng số
static constexpr double W_DIST = 0.7;
static constexpr double W_CHARGE = 0.3;

static double getRouteWeightedCost(const Route& route) {
    return W_DIST * route.getTotalDistance() + W_CHARGE * route.getTotalChargeAmount();
}

/**
 * @brief Constructor với cache trạm sạc.
 */
LocalSearch::LocalSearch(std::shared_ptr<Instance> inst) : instance(std::move(inst)) {
    for (const auto& node : instance->getNodes()) {
        if (std::dynamic_pointer_cast<Station>(node) && node->getId() > 1) {
            stationIds.push_back(node->getId());
        }
    }
}

/**
 * @brief (TỐI ƯU) Hàm run() với Static Move Descriptors.
 */
void LocalSearch::run(Solution& solution) {
    bool improved = true;

    // ===== STATIC MOVE DESCRIPTORS =====
    // Khai báo bên ngoài vòng lặp để tái sử dụng
    MoveDescriptor bestMove;
    MoveDescriptor relocateMove;
    MoveDescriptor swapMove;
    MoveDescriptor twoOptMove;
    MoveDescriptor stationInsertMove;
    MoveDescriptor stationRemoveMove;
    // int iter = 0;
    while (improved) {
        improved = false;
        // iter++;

        // Reset bestMove cho vòng lặp mới
        bestMove.eval.objectiveDelta = -1e-9;
        bestMove.eval.isFeasible = false;

        // --- 1. RELOCATE ---
        if (findBestRelocateMove(solution, relocateMove)) {
            if (relocateMove.eval.objectiveDelta < bestMove.eval.objectiveDelta) {
                bestMove = relocateMove;
            }
        }

        // --- 2. SWAP ---
        if (findBestSwapMove(solution, swapMove)) {
            if (swapMove.eval.objectiveDelta < bestMove.eval.objectiveDelta) {
                bestMove = swapMove;
            }
        }

        // --- 3. TWO_OPT ---
        if (findBestTwoOptMove(solution, twoOptMove)) {
            if (twoOptMove.eval.objectiveDelta < bestMove.eval.objectiveDelta) {
                bestMove = twoOptMove;
            }
        }

        // --- 4. STATION_INSERT ---
        if (findBestStationInsertionMove(solution, stationInsertMove)) {
            if (stationInsertMove.eval.objectiveDelta < bestMove.eval.objectiveDelta) {
                bestMove = stationInsertMove;
            }
        }

        // --- 5. STATION_REMOVE ---
        if (findBestStationRemovalMove(solution, stationRemoveMove)) {
            if (stationRemoveMove.eval.objectiveDelta < bestMove.eval.objectiveDelta) {
                bestMove = stationRemoveMove;
            }
        }

        // Áp dụng nước đi tốt nhất
        if (bestMove.eval.objectiveDelta < -1e-9) {
            applyMove(solution, bestMove);
            improved = true;
        }
    }
}

/**
 * @brief Áp dụng nước đi.
 */
void LocalSearch::applyMove(Solution& solution, const MoveDescriptor& move) {
    // (Logic này của bạn đã đúng, giữ nguyên)

    bool reEvaluateRoute1 = true;
    bool reEvaluateRoute2 = false;

    switch (move.type) {
        case MoveType::RELOCATE: {
            int customerId = solution.getRoutes()[move.routeIdx1].getNodes()[move.pos1];
            solution.getRoutes()[move.routeIdx1].removeNode(move.pos1);
            int targetPos = move.pos2;
            if (move.routeIdx1 == move.routeIdx2 && move.pos1 < move.pos2) {
                targetPos--;
            }
            solution.getRoutes()[move.routeIdx2].addNode(customerId, targetPos);
            if (move.routeIdx1 != move.routeIdx2) {
                reEvaluateRoute2 = true;
            }
            break;
        }

        case MoveType::SWAP: {
            int r1_idx = move.routeIdx1, r2_idx = move.routeIdx2;
            int p1 = move.pos1, p2 = move.pos2;
            int cust1 = solution.getRoutes()[r1_idx].getNodes()[p1];
            int cust2 = solution.getRoutes()[r2_idx].getNodes()[p2];

            if (r1_idx == r2_idx) {
                int pos_first = std::min(p1, p2), pos_second = std::max(p1, p2);
                int cust_first = (p1 < p2) ? cust1 : cust2;
                int cust_second = (p1 < p2) ? cust2 : cust1;
                Route& route = solution.getRoutes()[r1_idx];
                route.removeNode(pos_second);
                route.removeNode(pos_first);
                route.addNode(cust_second, pos_first);
                route.addNode(cust_first, pos_second);
            } else {
                Route& route1 = solution.getRoutes()[r1_idx];
                Route& route2 = solution.getRoutes()[r2_idx];
                route1.removeNode(p1);
                route2.removeNode(p2);
                route1.addNode(cust2, p1);
                route2.addNode(cust1, p2);
                reEvaluateRoute2 = true;
            }
            break;
        }

        case MoveType::TWO_OPT: {
            solution.getRoutes()[move.routeIdx1].reverseNodes(move.pos1, move.pos2);
            break;
        }

        case MoveType::STATION_INSERT: {
            solution.getRoutes()[move.routeIdx1].addNode(move.stationId, move.pos1);
            break;
        }

        case MoveType::STATION_REMOVE: {
            solution.getRoutes()[move.routeIdx1].removeNode(move.pos1);
            break;
        }

        default:
            std::cerr << "Warning: applyMove called with unknown move type." << std::endl;
            return;
    }

    if (reEvaluateRoute1) {
        solution.getRoutes()[move.routeIdx1].evaluate();
    }
    if (reEvaluateRoute2) {
        solution.getRoutes()[move.routeIdx2].evaluate();
    }

    solution.evaluateRoutes();
}

/**
 * @brief (ĐÃ SỬA) Đánh giá nước đi với Local Route Objects.
 */
MoveEvaluation LocalSearch::evaluateMove(const Solution& solution, const MoveDescriptor& move) {
    MoveEvaluation result;
    result.isFeasible = false;
    result.objectiveDelta = std::numeric_limits<double>::infinity();

    // ===== SỬA LỖI: KHÔNG DÙNG STATIC Ở ĐÂY =====
    // Khai báo các đối tượng Route tạm thời CỤC BỘ (local)
    // để đảm bảo chúng "sạch" (không có dữ liệu rác)

    if (move.type == MoveType::RELOCATE) {
        // Khai báo cục bộ
        Route tempRoute1 = solution.getRoutes()[move.routeIdx1];
        double oldCost1 = getRouteWeightedCost(tempRoute1);
        int customerId = tempRoute1.getNodes()[move.pos1];

        if (move.routeIdx1 == move.routeIdx2) {
            tempRoute1.removeNode(move.pos1);
            int targetPos = move.pos2;
            if (move.pos1 < move.pos2) targetPos--;
            tempRoute1.addNode(customerId, targetPos);
            tempRoute1.evaluate();
            if (tempRoute1.isFeasible()) {
                result.isFeasible = true;
                result.objectiveDelta = getRouteWeightedCost(tempRoute1) - oldCost1;
            }
        } else {
            // Khai báo cục bộ
            Route tempRoute2 = solution.getRoutes()[move.routeIdx2];
            double oldCost2 = getRouteWeightedCost(tempRoute2);
            tempRoute1.removeNode(move.pos1);
            tempRoute2.addNode(customerId, move.pos2);
            tempRoute1.evaluate();
            tempRoute2.evaluate();
            if (tempRoute1.isFeasible() && tempRoute2.isFeasible()) {
                result.isFeasible = true;
                result.objectiveDelta = (getRouteWeightedCost(tempRoute1) +
                                         getRouteWeightedCost(tempRoute2)) -
                                        (oldCost1 + oldCost2);
            }
        }
    }
    else if (move.type == MoveType::SWAP) {
        int r1_idx = move.routeIdx1, r2_idx = move.routeIdx2;
        int p1 = move.pos1, p2 = move.pos2;
        // Khai báo cục bộ
        Route tempRoute1 = solution.getRoutes()[r1_idx];
        int cust1 = tempRoute1.getNodes()[p1];
        double oldCost1 = getRouteWeightedCost(tempRoute1);

        if (r1_idx == r2_idx) {
            int cust2 = tempRoute1.getNodes()[p2];
            int pos_first = std::min(p1, p2), pos_second = std::max(p1, p2);
            int cust_first = (p1 < p2) ? cust1 : cust2;
            int cust_second = (p1 < p2) ? cust2 : cust1;
            tempRoute1.removeNode(pos_second);
            tempRoute1.removeNode(pos_first);
            tempRoute1.addNode(cust_second, pos_first);
            tempRoute1.addNode(cust_first, pos_second);
            tempRoute1.evaluate();
            if (tempRoute1.isFeasible()) {
                result.isFeasible = true;
                result.objectiveDelta = getRouteWeightedCost(tempRoute1) - oldCost1;
            }
        } else {
            // Khai báo cục bộ
            Route tempRoute2 = solution.getRoutes()[r2_idx];
            int cust2 = tempRoute2.getNodes()[p2];
            double oldCost2 = getRouteWeightedCost(tempRoute2);
            tempRoute1.removeNode(p1);
            tempRoute2.removeNode(p2);
            tempRoute1.addNode(cust2, p1);
            tempRoute2.addNode(cust1, p2);
            tempRoute1.evaluate();
            tempRoute2.evaluate();
            if (tempRoute1.isFeasible() && tempRoute2.isFeasible()) {
                result.isFeasible = true;
                result.objectiveDelta = (getRouteWeightedCost(tempRoute1) +
                                         getRouteWeightedCost(tempRoute2)) -
                                        (oldCost1 + oldCost2);
            }
        }
    }
    else if (move.type == MoveType::TWO_OPT) {
        // Khai báo cục bộ
        Route tempRoute1 = solution.getRoutes()[move.routeIdx1];
        double oldCost = getRouteWeightedCost(tempRoute1);
        tempRoute1.reverseNodes(move.pos1, move.pos2);
        tempRoute1.evaluate();
        if (tempRoute1.isFeasible()) {
            result.isFeasible = true;
            result.objectiveDelta = getRouteWeightedCost(tempRoute1) - oldCost;
        }
    }
    else if (move.type == MoveType::STATION_INSERT) {
        // Khai báo cục bộ
        Route tempRoute1 = solution.getRoutes()[move.routeIdx1];
        double oldCost = getRouteWeightedCost(tempRoute1);
        tempRoute1.addNode(move.stationId, move.pos1);
        tempRoute1.evaluate();
        if (tempRoute1.isFeasible()) {
            result.isFeasible = true;
            result.objectiveDelta = getRouteWeightedCost(tempRoute1) - oldCost;
        }
    }
    else if (move.type == MoveType::STATION_REMOVE) {
        // Khai báo cục bộ
        Route tempRoute1 = solution.getRoutes()[move.routeIdx1];
        double oldCost = getRouteWeightedCost(tempRoute1);
        tempRoute1.removeNode(move.pos1);
        tempRoute1.evaluate();
        if (tempRoute1.isFeasible()) {
            result.isFeasible = true;
            result.objectiveDelta = getRouteWeightedCost(tempRoute1) - oldCost;
        }
    }

    return result;
}

/**
 * @brief (TỐI ƯU) findBestRelocateMove với Static Move Descriptor. (ĐÚNG)
 */
bool LocalSearch::findBestRelocateMove(const Solution& solution, MoveDescriptor& out_bestMove) {
    out_bestMove.eval.objectiveDelta = std::numeric_limits<double>::infinity();
    out_bestMove.eval.isFeasible = false;
    bool foundFeasibleMove = false;

    // Static currentMove để tái sử dụng (ĐÚNG)
    static thread_local MoveDescriptor currentMove;

    for (size_t r1_idx = 0; r1_idx < solution.getRoutes().size(); ++r1_idx) {
        const Route& r1 = solution.getRoutes()[r1_idx];
        for (size_t p1_idx = 1; p1_idx < r1.getNodes().size() - 1; ++p1_idx) {
            int cust_id = r1.getNodes()[p1_idx];
            if (cust_id <= 0) continue;

            for (size_t r2_idx = 0; r2_idx < solution.getRoutes().size(); ++r2_idx) {
                for (size_t p2_idx = 1; p2_idx < solution.getRoutes()[r2_idx].getNodes().size(); ++p2_idx) {
                    if (r1_idx == r2_idx && (p2_idx == p1_idx || p2_idx == p1_idx + 1)) continue;

                    // Sử dụng lại currentMove
                    currentMove.type = MoveType::RELOCATE;
                    currentMove.customer1 = cust_id;
                    currentMove.routeIdx1 = r1_idx;
                    currentMove.pos1 = p1_idx;
                    currentMove.routeIdx2 = r2_idx;
                    currentMove.pos2 = p2_idx;
                    currentMove.eval = evaluateMove(solution, currentMove);

                    if (currentMove.eval.isFeasible &&
                        currentMove.eval.objectiveDelta < out_bestMove.eval.objectiveDelta) {
                        out_bestMove = currentMove;
                        foundFeasibleMove = true;
                    }
                }
            }
        }
    }
    return foundFeasibleMove;
}

/**
 * @brief (TỐI ƯU) findBestSwapMove với Static Move Descriptor. (ĐÚNG)
 */
bool LocalSearch::findBestSwapMove(const Solution& solution, MoveDescriptor& out_bestMove) {
    out_bestMove.eval.objectiveDelta = std::numeric_limits<double>::infinity();
    out_bestMove.eval.isFeasible = false;
    bool foundFeasibleMove = false;

    static thread_local MoveDescriptor currentMove;

    for (size_t r1_idx = 0; r1_idx < solution.getRoutes().size(); ++r1_idx) {
        const Route& r1 = solution.getRoutes()[r1_idx];
        for (size_t p1_idx = 1; p1_idx < r1.getNodes().size() - 1; ++p1_idx) {
            int cust1_id = r1.getNodes()[p1_idx];
            if (cust1_id <= 0) continue;

            for (size_t r2_idx = r1_idx; r2_idx < solution.getRoutes().size(); ++r2_idx) {
                const Route& r2 = solution.getRoutes()[r2_idx];
                size_t p2_start = (r1_idx == r2_idx) ? (p1_idx + 1) : 1;
                for (size_t p2_idx = p2_start; p2_idx < r2.getNodes().size() - 1; ++p2_idx) {
                    int cust2_id = r2.getNodes()[p2_idx];
                    if (cust2_id <= 0) continue;

                    currentMove.type = MoveType::SWAP;
                    currentMove.customer1 = cust1_id;
                    currentMove.customer2 = cust2_id;
                    currentMove.routeIdx1 = r1_idx;
                    currentMove.pos1 = p1_idx;
                    currentMove.routeIdx2 = r2_idx;
                    currentMove.pos2 = p2_idx;
                    currentMove.eval = evaluateMove(solution, currentMove);

                    if (currentMove.eval.isFeasible &&
                        currentMove.eval.objectiveDelta < out_bestMove.eval.objectiveDelta) {
                        out_bestMove = currentMove;
                        foundFeasibleMove = true;
                    }
                }
            }
        }
    }
    return foundFeasibleMove;
}

/**
 * @brief (TỐI ƯU) findBestTwoOptMove với Static Move Descriptor. (ĐÚNG)
 */
bool LocalSearch::findBestTwoOptMove(const Solution& solution, MoveDescriptor& out_bestMove) {
    out_bestMove.eval.objectiveDelta = std::numeric_limits<double>::infinity();
    out_bestMove.eval.isFeasible = false;
    bool foundFeasibleMove = false;

    static thread_local MoveDescriptor currentMove;

    for (size_t r_idx = 0; r_idx < solution.getRoutes().size(); ++r_idx) {
        const Route& route = solution.getRoutes()[r_idx];
        if (route.getNodes().size() < 5) continue;

        for (size_t i = 1; i < route.getNodes().size() - 2; ++i) {
            for (size_t j = i + 1; j < route.getNodes().size() - 1; ++j) {
                currentMove.type = MoveType::TWO_OPT;
                currentMove.routeIdx1 = r_idx;
                currentMove.pos1 = i;
                currentMove.pos2 = j;
                currentMove.eval = evaluateMove(solution, currentMove);

                if (currentMove.eval.isFeasible &&
                    currentMove.eval.objectiveDelta < out_bestMove.eval.objectiveDelta) {
                    out_bestMove = currentMove;
                    foundFeasibleMove = true;
                }
            }
        }
    }
    return foundFeasibleMove;
}

/**
 * @brief (TỐI ƯU) findBestStationInsertionMove với Static Move Descriptor. (ĐÚNG)
 */
bool LocalSearch::findBestStationInsertionMove(const Solution& solution, MoveDescriptor& out_bestMove) {
    out_bestMove.eval.objectiveDelta = std::numeric_limits<double>::infinity();
    out_bestMove.eval.isFeasible = false;
    bool foundFeasibleMove = false;

    static thread_local MoveDescriptor currentMove;

    for (size_t r_idx = 0; r_idx < solution.getRoutes().size(); ++r_idx) {
        for (int station_id : this->stationIds) {
            for (size_t pos = 1; pos < solution.getRoutes()[r_idx].getNodes().size(); ++pos) {
                currentMove.type = MoveType::STATION_INSERT;
                currentMove.routeIdx1 = r_idx;
                currentMove.pos1 = pos;
                currentMove.stationId = station_id;
                currentMove.eval = evaluateMove(solution, currentMove);

                if (currentMove.eval.isFeasible &&
                    currentMove.eval.objectiveDelta < out_bestMove.eval.objectiveDelta) {
                    out_bestMove = currentMove;
                    foundFeasibleMove = true;
                }
            }
        }
    }
    return foundFeasibleMove;
}

/**
 * @brief (TỐI ƯU) findBestStationRemovalMove với Static Move Descriptor. (ĐÚNG)
 */
bool LocalSearch::findBestStationRemovalMove(const Solution& solution, MoveDescriptor& out_bestMove) {
    out_bestMove.eval.objectiveDelta = std::numeric_limits<double>::infinity();
    out_bestMove.eval.isFeasible = false;
    bool foundFeasibleMove = false;

    static thread_local MoveDescriptor currentMove;

    for (size_t r_idx = 0; r_idx < solution.getRoutes().size(); ++r_idx) {
        const auto& nodes = solution.getRoutes()[r_idx].getNodes();
        for (size_t pos = 1; pos < nodes.size() - 1; ++pos) {
            int node_id = nodes[pos];

            if (std::dynamic_pointer_cast<Station>(instance->getNodeById(node_id))) {
                currentMove.type = MoveType::STATION_REMOVE;
                currentMove.routeIdx1 = r_idx;
                currentMove.pos1 = pos;
                currentMove.stationId = node_id;
                currentMove.eval = evaluateMove(solution, currentMove);

                if (currentMove.eval.isFeasible &&
                    currentMove.eval.objectiveDelta < out_bestMove.eval.objectiveDelta) {
                    out_bestMove = currentMove;
                    foundFeasibleMove = true;
                }
            }
        }
    }
    return foundFeasibleMove;
}