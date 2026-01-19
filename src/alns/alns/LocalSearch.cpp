#include "../../include/alns/LocalSearch.h"
#include "../../include/core/Customer.h"
#include "../../include/core/Station.h"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <limits>
#include <numeric>

LocalSearch::LocalSearch(std::shared_ptr<Instance> inst) : instance(inst) {
    for (const auto& node : instance->getNodes()) {
        if (node->getType() == NodeType::STATION) {
            stationIds.push_back(node->getId());
        }
    }

    // Preprocess K-Nearest Neighbors
    preprocessKNN();
    
    // Preprocess Granular Neighborhoods
    preprocessGranularity();
}

void LocalSearch::run(Solution& solution) {
    // ⭐ OPTIMIZATION: Reduced from 50 to 25 iterations (still effective)
    int maxIter = 25;
    
    for (int iter = 0; iter < maxIter; ++iter) {
        bool improved = false;
        
        // Distance optimization (most impactful)
        if (runDistanceOptimization(solution)) {
            improved = true;
        }
        
        // Charging optimization (less frequent for efficiency)
        if (iter % 2 == 0) {  // ⭐ Only every 2nd iteration
            if (runChargingOptimization(solution)) {
                improved = true;
            }
        }
        
        // Vehicle reduction (expensive, only when needed)
        if ((iter % 5 == 0 || !improved) && solution.getRoutes().size() > 1) {
            if (runVehicleReduction(solution)) {
                improved = true;
                noImprovementCount_ = 0; // Reset on vehicle reduction
            }
        }
        
        if (improved) {
            solution.evaluateRoutes();
            noImprovementCount_ = 0;
            // Shrink neighborhood on success (intensification)
            maxNodesToCheck_ = std::max(MIN_NODES_TO_CHECK, maxNodesToCheck_ - 1);
            maxSwapAttempts_ = std::max(MIN_SWAP_ATTEMPTS, maxSwapAttempts_ - 1);
        } else {
            noImprovementCount_++;
            // Expand neighborhood on failure (diversification)
            maxNodesToCheck_ = std::min(MAX_NODES_TO_CHECK, maxNodesToCheck_ + 1);
            maxSwapAttempts_ = std::min(MAX_SWAP_ATTEMPTS, maxSwapAttempts_ + 1);
            
            // Early termination after 2 iterations
            if (noImprovementCount_ > 2) break;
        }
    }
}

void LocalSearch::evaluateMove(const Solution& solution, MoveDescriptor& move, const LocalSearchWeights& weights) {
    move.eval.reset();
    move.eval.isFeasible = false;
    const auto& routes = solution.getRoutes();
    Route r1_copy = routes[move.routeIdx1];
    
    bool twoRoutes = (move.routeIdx1 != move.routeIdx2 && move.routeIdx2 >= 0);
    Route r2_copy = twoRoutes ? routes[move.routeIdx2] : r1_copy;

    try {
        switch (move.type) {
            case MoveType::INTRA_RELOCATE:
            case MoveType::INTER_RELOCATE: {
                int nodeId = r1_copy.getNodeAt(move.nodeIdx1);
                r1_copy.removeNode(move.nodeIdx1);
                if (twoRoutes) {
                    r2_copy.addNode(nodeId, move.nodeIdx2);
                } else {
                    int targetIdx = move.nodeIdx2;
                    if (move.nodeIdx1 < move.nodeIdx2) targetIdx--;
                    r1_copy.addNode(nodeId, targetIdx);
                }
                break;
            }
            case MoveType::INTRA_OR_OPT:
            case MoveType::INTER_OR_OPT: {
                std::vector<int> segment;
                segment.reserve(move.segmentLength);
                for (int i = 0; i < move.segmentLength; ++i) {
                    segment.push_back(r1_copy.getNodeAt(move.nodeIdx1 + i));
                }

                // Quan trọng: Xóa node từ cuối lên đầu để không làm thay đổi chỉ số
                for (int i = move.segmentLength - 1; i >= 0; --i) {
                    r1_copy.removeNode(move.nodeIdx1 + i);
                }

                if (twoRoutes) {
                    for (int i = 0; i < move.segmentLength; ++i) {
                        r2_copy.addNode(segment[i], move.nodeIdx2 + i);
                    }
                } else { // Cùng một route
                    int targetIdx = move.nodeIdx2;
                    // Điều chỉnh chỉ số nếu di chuyển về phía sau trong cùng một route
                    if (move.nodeIdx1 < move.nodeIdx2) {
                        targetIdx -= move.segmentLength;
                    }
                    for (int i = 0; i < move.segmentLength; ++i) {
                        r1_copy.addNode(segment[i], targetIdx + i);
                    }
                }
                break;
            }
            case MoveType::INTRA_TWO_OPT:
                r1_copy.reverseNodes(move.nodeIdx1, move.nodeIdx2);
                break;
            case MoveType::INTRA_SWAP: {
                int nodeId1 = r1_copy.getNodeAt(move.nodeIdx1);
                int nodeId2 = r1_copy.getNodeAt(move.nodeIdx2);
                r1_copy.removeNode(move.nodeIdx1);
                r1_copy.addNode(nodeId2, move.nodeIdx1);
                r1_copy.removeNode(move.nodeIdx2);
                r1_copy.addNode(nodeId1, move.nodeIdx2);
                break;
            }
            case MoveType::INTER_SWAP: {
                if (!twoRoutes) return; // Sanity check
                int nodeId1 = r1_copy.getNodeAt(move.nodeIdx1);
                int nodeId2 = r2_copy.getNodeAt(move.nodeIdx2);
                r1_copy.removeNode(move.nodeIdx1);
                r2_copy.removeNode(move.nodeIdx2);
                r1_copy.addNode(nodeId2, move.nodeIdx1);
                r2_copy.addNode(nodeId1, move.nodeIdx2);
                break;
            }
            case MoveType::STATION_REMOVAL: {
                r1_copy.removeNode(move.nodeIdx1);
                break;
            }
            default: return;
        }

        r1_copy.evaluate();
        if (twoRoutes) r2_copy.evaluate();

        if (!r1_copy.isFeasible() || (twoRoutes && !r2_copy.isFeasible())) {
            return;
        }

        double newDist = r1_copy.getTotalDistance() + (twoRoutes ? r2_copy.getTotalDistance() : 0);
        double oldDist = routes[move.routeIdx1].getTotalDistance() + (twoRoutes ? routes[move.routeIdx2].getTotalDistance() : 0);
        
        move.eval.isFeasible = true;
        move.eval.distanceDelta = newDist - oldDist;
        move.eval.objectiveDelta = move.eval.distanceDelta * weights.dist;

    } catch (...) {
        move.eval.isFeasible = false;
    }
}

void LocalSearch::applyMove(Solution& solution, const MoveDescriptor& move) {
    auto& routes = solution.getRoutes();

    switch (move.type) {
        case MoveType::INTRA_RELOCATE: {
            Route& r1 = routes[move.routeIdx1];
            int nodeId = r1.getNodeAt(move.nodeIdx1);
            r1.removeNode(move.nodeIdx1);
            int targetIdx = move.nodeIdx2;
            if (move.nodeIdx1 < move.nodeIdx2) targetIdx--;
            r1.addNode(nodeId, targetIdx);
            r1.evaluate();
            break;
        }
        case MoveType::INTER_RELOCATE: {
            Route& r1 = routes[move.routeIdx1];
            Route& r2 = routes[move.routeIdx2];
            int nodeId = r1.getNodeAt(move.nodeIdx1);
            r1.removeNode(move.nodeIdx1);
            r2.addNode(nodeId, move.nodeIdx2);
            r1.evaluate();
            r2.evaluate();
            break;
        }
        case MoveType::INTRA_OR_OPT: {
            Route& r1 = routes[move.routeIdx1];
            std::vector<int> segment;
            segment.reserve(move.segmentLength);
            for (int i = 0; i < move.segmentLength; ++i) {
                segment.push_back(r1.getNodeAt(move.nodeIdx1 + i));
            }

            for (int i = move.segmentLength - 1; i >= 0; --i) {
                r1.removeNode(move.nodeIdx1 + i);
            }

            int targetIdx = move.nodeIdx2;
            if (move.nodeIdx1 < move.nodeIdx2) {
                targetIdx -= move.segmentLength;
            }
            for (int i = 0; i < move.segmentLength; ++i) {
                r1.addNode(segment[i], targetIdx + i);
            }
            r1.evaluate();
            break;
        }
        case MoveType::INTER_OR_OPT: {
            Route& r1 = routes[move.routeIdx1];
            Route& r2 = routes[move.routeIdx2];
            std::vector<int> segment;
            segment.reserve(move.segmentLength);
            for (int i = 0; i < move.segmentLength; ++i) {
                segment.push_back(r1.getNodeAt(move.nodeIdx1 + i));
            }

            for (int i = move.segmentLength - 1; i >= 0; --i) {
                r1.removeNode(move.nodeIdx1 + i);
            }

            for (int i = 0; i < move.segmentLength; ++i) {
                r2.addNode(segment[i], move.nodeIdx2 + i);
            }
            r1.evaluate();
            r2.evaluate();
            break;
        }
        case MoveType::INTRA_TWO_OPT: {
            Route& r1 = routes[move.routeIdx1];
            r1.reverseNodes(move.nodeIdx1, move.nodeIdx2);
            r1.evaluate();
            break;
        }
        case MoveType::INTRA_SWAP: {
            Route& r1 = routes[move.routeIdx1];
            int nodeId1 = r1.getNodeAt(move.nodeIdx1);
            int nodeId2 = r1.getNodeAt(move.nodeIdx2);
            r1.removeNode(move.nodeIdx1);
            r1.addNode(nodeId2, move.nodeIdx1);
            r1.removeNode(move.nodeIdx2);
            r1.addNode(nodeId1, move.nodeIdx2);
            r1.evaluate();
            break;
        }
        case MoveType::INTER_SWAP: {
            Route& r1 = routes[move.routeIdx1];
            Route& r2 = routes[move.routeIdx2];
            int nodeId1 = r1.getNodeAt(move.nodeIdx1);
            int nodeId2 = r2.getNodeAt(move.nodeIdx2);
            r1.removeNode(move.nodeIdx1);
            r2.removeNode(move.nodeIdx2);
            r1.addNode(nodeId2, move.nodeIdx1);
            r2.addNode(nodeId1, move.nodeIdx2);
            r1.evaluate();
            r2.evaluate();
            break;
        }
        case MoveType::STATION_REMOVAL: {
            Route& r1 = routes[move.routeIdx1];
            r1.removeNode(move.nodeIdx1);
            r1.evaluate();
            break;
        }
        default:
            // Should not happen
            break;
    }
}

bool LocalSearch::runDistanceOptimization(Solution& solution) {
    LocalSearchWeights weights;
    weights.dist = 1.0;
    MoveDescriptor bestMove;
    
    double scoreRelocate = relocateStats_.getScore();
    double scoreSwap = swapStats_.getScore();
    double scoreOrOpt = orOptStats_.getScore();
    double scoreTwoOpt = twoOptStats_.getScore();
    double totalScore = scoreRelocate + scoreSwap + scoreOrOpt + scoreTwoOpt;

    // --- Roulette Wheel Selection ---
    double pick = (static_cast<double>(rand()) / RAND_MAX) * totalScore;

    double oldDistance = solution.getTotalDistance();
    bool improved = false;

    if (pick < scoreRelocate) {
        relocateStats_.attempts++;
        if (searchRelocate(solution, bestMove, weights)) {
            improved = true;
            relocateStats_.successes++;
            relocateStats_.totalImprovement += oldDistance - solution.getTotalDistance();
        }
    } else if (pick < scoreRelocate + scoreSwap) {
        swapStats_.attempts++;
        if (searchSwap(solution, bestMove, weights)) {
            improved = true;
            swapStats_.successes++;
            swapStats_.totalImprovement += oldDistance - solution.getTotalDistance();
        }
    } else if (pick < scoreRelocate + scoreSwap + scoreOrOpt) {
        orOptStats_.attempts++;
        if (searchOrOpt(solution, bestMove, weights)) {
            improved = true;
            orOptStats_.successes++;
            orOptStats_.totalImprovement += oldDistance - solution.getTotalDistance();
        }
    } else {
        twoOptStats_.attempts++;
        if (searchTwoOpt(solution, bestMove, weights)) {
            applyMove(solution, bestMove);
            improved = true;
            twoOptStats_.successes++;
            twoOptStats_.totalImprovement += oldDistance - solution.getTotalDistance();
        }
    }

    return improved;
}

bool LocalSearch::searchRelocate(Solution& solution, MoveDescriptor& outBestMove, const LocalSearchWeights& weights) {
    const auto& routes = solution.getRoutes();
    int numRoutes = routes.size();

    auto centroids = computeAllCentroids(solution);
    std::vector<std::vector<int>> neighborLists(numRoutes);
    for (int r1 = 0; r1 < numRoutes; ++r1) {
        for (int r2 = 0; r2 < numRoutes; ++r2) {
            if (r1 == r2 || areRoutesClose(centroids[r1], centroids[r2], distanceThreshold_)) {
                neighborLists[r1].push_back(r2);
            }
        }
    }

    // ⭐ SMD: Use member variable instead of local
    for (int r1 = 0; r1 < numRoutes; ++r1) {
        if (routes[r1].size() <= 2) continue;

        auto rankedNodes = rankNodesByRemovalSavings(routes[r1]);
        
        int nodesToCheck = std::min(maxNodesToCheck_, (int)rankedNodes.size());

        for (int k = 0; k < nodesToCheck; ++k) {
            int i = rankedNodes[k].first;
            int nodeId = routes[r1].getNodeAt(i);
            if (instance->getNodeById(nodeId)->getType() != NodeType::CUSTOMER) continue;

            for (int r2 : neighborLists[r1]) {
                
                // ⭐ Use KNN to find candidate positions
                auto candidatePositions = findBestInsertionPositions_KNN(routes[r2], nodeId, 3);

                for (size_t j : candidatePositions) {
                    if (r1 == r2 && (j == i || j == i + 1)) continue;

                    // Tier 3: Comprehensive Check
                    if (!routes[r2].canPossiblyInsert(nodeId, j)) {
                        continue;
                    }
                    
                    activeMove.reset(); // ⭐ SMD: Reuse member
                    activeMove.type = (r1 == r2) ? MoveType::INTRA_RELOCATE : MoveType::INTER_RELOCATE;
                    activeMove.routeIdx1 = r1; activeMove.nodeIdx1 = i;
                    activeMove.routeIdx2 = r2; activeMove.nodeIdx2 = j;

                    evaluateMove(solution, activeMove, weights);

                    if (activeMove.eval.isFeasible && activeMove.eval.objectiveDelta < -1e-9) {
                        // ⭐ First Improvement: Apply immediately
                        applyMove(solution, activeMove);
                        return true;
                    }
                }
            }
        }
    }
    return false; // No improvement found
}



bool LocalSearch::searchTwoOpt(Solution& solution, MoveDescriptor& outBestMove, const LocalSearchWeights& weights) {
    outBestMove.reset();
    outBestMove.eval.objectiveDelta = -1e-5;
    bool found = false;
    
    auto& routes = solution.getRoutes();
    
    // ⭐ SMD: Use member variable instead of local
    // 2-Opt chỉ áp dụng cho INTRA-ROUTE (đảo ngược segment trong cùng route)
    for (int r = 0; r < routes.size(); ++r) {
        const auto& nodes = routes[r].getNodes();
        int n = nodes.size();
        
        // Cần ít nhất 4 nodes: depot_start - node1 - node2 - depot_end
        if (n < 4) continue;
        
        // Thử tất cả các cặp (i, j) với i < j
        // Segment [i, j] sẽ bị đảo ngược
        for (int i = 1; i < n - 2; ++i) {
            for (int j = i + 1; j < n - 1; ++j) {
                // Skip nếu segment quá ngắn (không có gì để optimize)
                if (j - i < 1) continue;
                
                // Quick check: Chỉ evaluate nếu có potential savings
                // Compare: (i-1 -> i) + (j -> j+1) vs (i-1 -> j) + (i -> j+1)
                int prev = nodes[i - 1];
                int nodeI = nodes[i];
                int nodeJ = nodes[j];
                int next = nodes[j + 1];
                
                double oldDist = instance->getDistance(prev, nodeI) + 
                                instance->getDistance(nodeJ, next);
                double newDist = instance->getDistance(prev, nodeJ) + 
                                instance->getDistance(nodeI, next);
                
                // Quick reject nếu không có savings
                if (newDist >= oldDist - 1e-6) continue;
                
                // Tạo move descriptor
                activeMove.reset(); // ⭐ SMD: Reuse member
                activeMove.type = MoveType::INTRA_TWO_OPT;
                activeMove.routeIdx1 = r;
                activeMove.nodeIdx1 = i;
                activeMove.routeIdx2 = r;  // Same route
                activeMove.nodeIdx2 = j;
                
                // Full evaluation
                evaluateMove(solution, activeMove, weights);
                
                if (activeMove.eval.isFeasible && 
                    activeMove.eval.objectiveDelta < outBestMove.eval.objectiveDelta) {
                    outBestMove = activeMove; // ⭐ Best Improvement: Track best
                    found = true;
                }
            }
        }
    }
    
    return found;
}
bool LocalSearch::searchSwap(Solution& solution, MoveDescriptor& outBestMove, const LocalSearchWeights& weights) {
    const auto& routes = solution.getRoutes();
    int numRoutes = routes.size();
    
    auto centroids = computeAllCentroids(solution);
    
    // ⭐ SMD: Use member variable instead of local
    // INTRA-ROUTE SWAP
    for (int r = 0; r < numRoutes; ++r) {
        const auto& nodes = routes[r].getNodes();
        int n = nodes.size();
        if (n < 4) continue;
        
        for (int i = 1; i < n - 2; ++i) {
            if (instance->getNodeById(nodes[i])->getType() != NodeType::CUSTOMER) continue;
            
            for (int j = i + 2; j < n - 1; ++j) {
                if (instance->getNodeById(nodes[j])->getType() != NodeType::CUSTOMER) continue;
                
                int prev_i = nodes[i - 1], next_i = nodes[i + 1];
                int prev_j = nodes[j - 1], next_j = nodes[j + 1];
                double oldDist = instance->getDistance(prev_i, nodes[i]) + instance->getDistance(nodes[i], next_i) +
                                 instance->getDistance(prev_j, nodes[j]) + instance->getDistance(nodes[j], next_j);
                double newDist = instance->getDistance(prev_i, nodes[j]) + instance->getDistance(nodes[j], next_i) +
                                 instance->getDistance(prev_j, nodes[i]) + instance->getDistance(nodes[i], next_j);
                
                if (newDist >= oldDist - 1e-6) continue;
                
                // Tier 3: Check if swap is feasible (capacity, energy, time window)
                // We need to check if we can insert node[j] at position i (removing node[i])
                // and insert node[i] at position j (removing node[j])
                if (!routes[r].canPossiblyInsert(nodes[j], i, nodes[i]) ||
                    !routes[r].canPossiblyInsert(nodes[i], j, nodes[j])) {
                    continue;
                }
                
                activeMove.reset(); // ⭐ SMD: Reuse member
                activeMove.type = MoveType::INTRA_SWAP;
                activeMove.routeIdx1 = r; activeMove.nodeIdx1 = i;
                activeMove.routeIdx2 = r; activeMove.nodeIdx2 = j;
                
                evaluateMove(solution, activeMove, weights);
                
                if (activeMove.eval.isFeasible && activeMove.eval.objectiveDelta < -1e-9) {
                    // ⭐ First Improvement: Apply immediately
                    applyMove(solution, activeMove);
                    return true;
                }
            }
        }
    }
    
    // INTER-ROUTE SWAP
    for (int r1 = 0; r1 < numRoutes; ++r1) {
        for (int r2 = r1 + 1; r2 < numRoutes; ++r2) {
            // ⭐ GRANULAR FILTER (Route-level): Only consider close routes
            if (!areRoutesClose(centroids[r1], centroids[r2], distanceThreshold_)) continue;
            
            const auto& nodes1 = routes[r1].getNodes();
            const auto& nodes2 = routes[r2].getNodes();
            
            auto ranked1 = rankNodesByRemovalSavings(routes[r1]);
            auto ranked2 = rankNodesByRemovalSavings(routes[r2]);
            
            int attempts1 = std::min(maxSwapAttempts_, (int)ranked1.size());
            int attempts2 = std::min(maxSwapAttempts_, (int)ranked2.size());
            
            for (int k1 = 0; k1 < attempts1; ++k1) {
                int i = ranked1[k1].first;
                if (instance->getNodeById(nodes1[i])->getType() != NodeType::CUSTOMER) continue;
                
                for (int k2 = 0; k2 < attempts2; ++k2) {
                    int j = ranked2[k2].first;
                    if (instance->getNodeById(nodes2[j])->getType() != NodeType::CUSTOMER) continue;
                    
                    // ⭐ GRANULAR FILTER (Node-level): Only swap if customers are close
                    int nodeId1 = nodes1[i];
                    int nodeId2 = nodes2[j];
                    double distance = instance->getDistance(nodeId1, nodeId2);
                    if (distance >= distanceThreshold_) {
                        continue;  // Skip distant customer pairs
                    }

                    if (!routes[r1].canPossiblyInsert(nodeId2, i, nodeId1) ||
                        !routes[r2].canPossiblyInsert(nodeId1, j, nodeId2)) {
                        continue;
                    }

                    activeMove.reset(); // ⭐ SMD: Reuse member
                    activeMove.type = MoveType::INTER_SWAP;
                    activeMove.routeIdx1 = r1; activeMove.nodeIdx1 = i;
                    activeMove.routeIdx2 = r2; activeMove.nodeIdx2 = j;
                    
                    evaluateMove(solution, activeMove, weights);
                    
                    if (activeMove.eval.isFeasible && activeMove.eval.objectiveDelta < -1e-9) {
                        // ⭐ First Improvement: Apply immediately
                        applyMove(solution, activeMove);
                        return true;
                    }
                }
            }
        }
    }
    
    return false; // No improvement found
}

bool LocalSearch::searchOrOpt(Solution& solution, MoveDescriptor& outBestMove, const LocalSearchWeights& weights) {
    const int SEGMENT_LENGTH = 2;
    const auto& routes = solution.getRoutes();
    int numRoutes = routes.size();

    for (int r1 = 0; r1 < numRoutes; ++r1) {
        // Một đoạn dài 2 cần ít nhất 2 khách hàng + 2 depot = 4 nodes
        if (routes[r1].size() < 2 + SEGMENT_LENGTH) continue;

        // Lặp qua tất cả các vị trí bắt đầu của đoạn
        for (int i = 1; i <= routes[r1].size() - 1 - SEGMENT_LENGTH; ++i) {
            
            // --- Lọc: Kiểm tra đoạn có phải toàn khách hàng không ---
            bool segmentIsCustomers = true;
            for (int k = 0; k < SEGMENT_LENGTH; ++k) {
                if (instance->getNodeById(routes[r1].getNodeAt(i + k))->getType() != NodeType::CUSTOMER) {
                    segmentIsCustomers = false;
                    break;
                }
            }
            if (!segmentIsCustomers) continue;

            int firstNodeId = routes[r1].getNodeAt(i);

            // Thử chèn đoạn này vào tất cả các route khác (và cả route này)
            for (int r2 = 0; r2 < numRoutes; ++r2) {
                // ⭐ Use KNN to find candidate positions based on the first node of the segment
                auto candidatePositions = findBestInsertionPositions_KNN(routes[r2], firstNodeId, 3);

                for (int j : candidatePositions) {
                    
                    // --- Lọc: Không chèn lại vào vị trí cũ ---
                    if (r1 == r2 && (j >= i && j <= i + SEGMENT_LENGTH)) {
                        continue;
                    }
                    
                    activeMove.reset();
                    activeMove.type = (r1 == r2) ? MoveType::INTRA_OR_OPT : MoveType::INTER_OR_OPT;
                    activeMove.routeIdx1 = r1;
                    activeMove.nodeIdx1 = i;
                    activeMove.routeIdx2 = r2;
                    activeMove.nodeIdx2 = j;
                    activeMove.segmentLength = SEGMENT_LENGTH;

                    evaluateMove(solution, activeMove, weights);

                    if (activeMove.eval.isFeasible && activeMove.eval.objectiveDelta < -1e-9) {
                        // First Improvement: Áp dụng ngay
                        applyMove(solution, activeMove);
                        return true;
                    }
                }
            }
        }
    }
    return false; // Không tìm thấy cải thiện
}
bool LocalSearch::runChargingOptimization(Solution& solution) {
    bool improved = false;
    
    // Strategy 1: Remove redundant charging stations
    MoveDescriptor bestRemoval;
    LocalSearchWeights weights;
    weights.dist = 1.0;
    weights.time = 0.0;
    
    if (searchStationRemoval(solution, bestRemoval, weights)) {
        applyMove(solution, bestRemoval);
        improved = true;
    }
    
    // Strategy 2: Reposition stations (swap station with nearest neighbor station)
    if (repositionStations(solution)) {
        improved = true;
    }
    
    // Strategy 3: Adjust charging amounts (charge only what's needed)
    if (optimizeChargingAmounts(solution)) {
        improved = true;
    }
    
    return improved;
}

bool LocalSearch::searchStationRemoval(Solution& solution, MoveDescriptor& outBestMove, const LocalSearchWeights& weights) {
    outBestMove.reset();
    outBestMove.eval.objectiveDelta = -1e-5;
    bool found = false;

    auto& routes = solution.getRoutes();

    // Duyệt tất cả các routes để tìm stations có thể loại bỏ
    for (int r = 0; r < routes.size(); ++r) {
        const auto& nodes = routes[r].getNodes();

        // Tìm tất cả stations trong route
        std::vector<int> stationPositions;
        for (int i = 1; i < nodes.size() - 1; ++i) {
            if (instance->getNodeById(nodes[i])->getType() == NodeType::STATION) {
                stationPositions.push_back(i);
            }
        }

        // Thử loại bỏ từng station
        for (int stationPos : stationPositions) {
            // Tạo route copy và xóa station
            Route routeCopy = routes[r];
            routeCopy.removeNode(stationPos);
            routeCopy.evaluate();

            // Check feasibility
            if (!routeCopy.isFeasible()) continue;

            // Tính delta
            double oldDist = routes[r].getTotalDistance();
            double newDist = routeCopy.getTotalDistance();
            double delta = newDist - oldDist;

            // Station removal nên làm giảm distance (vì bỏ detour)
            if (delta < outBestMove.eval.objectiveDelta) {
                outBestMove.type = MoveType::STATION_REMOVAL;
                outBestMove.routeIdx1 = r;
                outBestMove.nodeIdx1 = stationPos;
                outBestMove.routeIdx2 = -1;
                outBestMove.nodeIdx2 = -1;
                outBestMove.eval.isFeasible = true;
                outBestMove.eval.distanceDelta = delta;
                outBestMove.eval.objectiveDelta = delta;
                found = true;
            }
        }
    }

    return found;
}

bool LocalSearch::repositionStations(Solution& solution) {
    auto& routes = solution.getRoutes();
    
    for (int r = 0; r < routes.size(); ++r) {
        // Lấy nodes ở đây để đảm bảo luôn làm việc với phiên bản mới nhất
        const auto& nodes = routes[r].getNodes(); 
        
        for (size_t i = 1; i < nodes.size() - 1; ++i) {
            int currentStationId = nodes[i];
            if (instance->getNodeById(currentStationId)->getType() != NodeType::STATION) continue;
            
            int prevNodeId = nodes[i - 1];
            int nextNodeId = nodes[i + 1];
            double currentDetour = instance->getDistance(prevNodeId, currentStationId) + 
                                  instance->getDistance(currentStationId, nextNodeId);
            
            int bestStationId = -1;
            double bestDetour = currentDetour;

            for (int altStationId : stationIds) {
                if (altStationId == currentStationId) continue;
                
                double altDetour = instance->getDistance(prevNodeId, altStationId) + 
                                  instance->getDistance(altStationId, nextNodeId);
                
                if (altDetour < bestDetour - 1e-6) {
                    auto altStation = instance->getNodeById(altStationId);
                    const auto& prevState = routes[r].getStates()[i - 1];
                    double arrivalTime = prevState.departureTime + instance->getTime(prevNodeId, altStationId);
                    
                    if (arrivalTime <= altStation->getDueDate()) {
                        bestDetour = altDetour;
                        bestStationId = altStationId;
                    }
                }
            }
            
            if (bestStationId != -1) {
                Route testRoute = routes[r];
                testRoute.removeNode(i);
                testRoute.addNode(bestStationId, i);
                testRoute.evaluate();
                
                if (testRoute.isFeasible() && testRoute.getTotalDistance() < routes[r].getTotalDistance()) {
                    routes[r] = testRoute;
                    return true; // <-- SỬA LỖI: First Improvement, return ngay
                }
            }
        }
    }
    
    return false; // Không tìm thấy cải thiện nào
}

bool LocalSearch::optimizeChargingAmounts(Solution& solution) {
    // Strategy: Detect and remove redundant charging stations
    // Since Route::evaluate() already optimizes charging amounts automatically,
    // this function focuses on removing unnecessary stations (especially consecutive ones)
    
    auto& routes = solution.getRoutes();
    bool improved = false;
    
    for (int r = 0; r < routes.size(); ++r) {
        const auto& nodes = routes[r].getNodes();
        
        // Find consecutive or nearby stations that might be redundant
        std::vector<size_t> stationPositions;
        for (size_t i = 1; i < nodes.size() - 1; ++i) {
            if (instance->getNodeById(nodes[i])->getType() == NodeType::STATION) {
                stationPositions.push_back(i);
            }
        }
        
        // If there are multiple stations, try to remove redundant ones
        if (stationPositions.size() >= 2) {
            // Strategy 1: Remove consecutive stations (keep only one)
            for (size_t idx = 0; idx < stationPositions.size() - 1; ++idx) {
                size_t pos1 = stationPositions[idx];
                size_t pos2 = stationPositions[idx + 1];
                
                // Check if they are close (within 3 positions)
                if (pos2 - pos1 <= 3) {
                    // Try removing the first one
                    Route testRoute = routes[r];
                    testRoute.removeNode(pos1);
                    testRoute.evaluate();
                    
                    if (testRoute.isFeasible()) {
                        routes[r] = testRoute;
                        improved = true;
                        break; // Re-evaluate route structure
                    }
                    
                    // If that didn't work, try removing the second one
                    testRoute = routes[r];
                    testRoute.removeNode(pos2);
                    testRoute.evaluate();
                    
                    if (testRoute.isFeasible()) {
                        routes[r] = testRoute;
                        improved = true;
                        break;
                    }
                }
            }
        }
        
        // Strategy 2: Try removing stations one by one (greedy)
        // This is complementary to searchStationRemoval but more aggressive
        if (!improved && stationPositions.size() > 0) {
            for (size_t pos : stationPositions) {
                Route testRoute = routes[r];
                testRoute.removeNode(pos);
                testRoute.evaluate();
                
                if (testRoute.isFeasible() && testRoute.getTotalDistance() < routes[r].getTotalDistance()) {
                    routes[r] = testRoute;
                    improved = true;
                    break; // First improvement
                }
            }
        }
    }
    
    return improved;
}

bool LocalSearch::runVehicleReduction(Solution& solution) {
    auto& routes = solution.getRoutes();
    if (routes.size() < 2) return false;

    auto centroids = computeAllCentroids(solution);
    
    // Sắp xếp các routes theo số lượng khách hàng tăng dần
    std::vector<int> routeIndices(routes.size());
    std::iota(routeIndices.begin(), routeIndices.end(), 0);
    std::sort(routeIndices.begin(), routeIndices.end(), [&](int a, int b) {
        return routes[a].size() < routes[b].size();
    });

    // ⭐ OPTIMIZATION: Reduced from 5 to 3 (only merge very small routes)
    const int MAX_CUSTOMERS_TO_MERGE = 3;

    for (int i = 0; i < routeIndices.size(); ++i) {
        int r1_idx = routeIndices[i];
        const auto& r1 = routes[r1_idx];

        // Chỉ thử gộp các route nhỏ
        if (r1.getCustomers().size() == 0 || r1.getCustomers().size() > MAX_CUSTOMERS_TO_MERGE) continue;

        for (int j = i + 1; j < routeIndices.size(); ++j) {
            int r2_idx = routeIndices[j];
            const auto& r2 = routes[r2_idx];

            // ⭐ GRANULAR FILTER: Only try merging close routes
            if (!areRoutesClose(centroids[r1_idx], centroids[r2_idx], distanceThreshold_)) continue;

            // Thử gộp r1 vào r2
            Solution testSol = solution;
            if (mergeRoutes(testSol, r2_idx, r1_idx)) {
                solution = testSol; // Áp dụng giải pháp mới
                return true; // First improvement
            }
        }
    }
    return false;
}

bool LocalSearch::mergeRoutes(Solution& solution, int targetRouteIdx, int sourceRouteIdx) {
    auto& routes = solution.getRoutes();
    Route& targetRoute = routes[targetRouteIdx];
    const Route& sourceRoute = routes[sourceRouteIdx];

    // Lấy danh sách khách hàng từ route nguồn
    auto sourceCustomers = sourceRoute.getCustomers();
    if (sourceCustomers.empty()) return false;

    // Thêm khách hàng vào cuối route đích
    for (int customerId : sourceCustomers) {
        targetRoute.addNode(customerId, targetRoute.size() - 1);
    }

    // Đánh giá lại route đích
    targetRoute.evaluate();

    if (targetRoute.isFeasible()) {
        // Xóa route nguồn (đã trống) khỏi giải pháp
        solution.removeRoute(sourceRouteIdx);
        return true;
    }

    return false; // Gộp không thành công
}

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
    if (count == 0) {
        auto depot = instance->getNodeById(0);
        return {depot->getX(), depot->getY()};
    }
    return {sumX / count, sumY / count};
}

std::vector<LocalSearch::RouteCentroid> LocalSearch::computeAllCentroids(const Solution& solution) {
    std::vector<RouteCentroid> centroids;
    centroids.reserve(solution.getRoutes().size());
    for(const auto& route : solution.getRoutes()) {
        centroids.push_back(computeCentroid(route));
    }
    return centroids;
}

bool LocalSearch::areRoutesClose(const RouteCentroid& c1, const RouteCentroid& c2, double threshold) const {
    double dist = std::sqrt(std::pow(c1.x - c2.x, 2) + std::pow(c1.y - c2.y, 2));
    return dist < threshold;
}

std::vector<std::pair<int, double>> LocalSearch::rankNodesByRemovalSavings(const Route& route) {
    std::vector<std::pair<int, double>> rankings;
    const auto& nodes = route.getNodes();
    if (nodes.size() <= 2) return rankings;

    for (size_t i = 1; i < nodes.size() - 1; ++i) {
        int prevNodeId = nodes[i-1];
        int currNodeId = nodes[i];
        int nextNodeId = nodes[i+1];
        
        double oldDist = instance->getDistance(prevNodeId, currNodeId) + instance->getDistance(currNodeId, nextNodeId);
        double newDist = instance->getDistance(prevNodeId, nextNodeId);
        double savings = oldDist - newDist;
        rankings.push_back({(int)i, savings});
    }
    
    std::sort(rankings.begin(), rankings.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });
    
    return rankings;
}

// Public wrapper
std::vector<size_t> LocalSearch::findBestInsertionPositions(const Route& route, int nodeId, int topK) const {
    // For now, we use the TimeAware heuristic as recommended.
    return findBestInsertionPositions_TimeAware(route, nodeId, topK);
}

// Time-Aware Heuristic Implementation
std::vector<size_t> LocalSearch::findBestInsertionPositions_TimeAware(const Route& route, int nodeId, int topK) const {
    struct Candidate {
        size_t position;
        double cost;
        bool operator<(const Candidate& o) const { return cost < o.cost; }
    };
    
    std::vector<Candidate> candidates;
    const auto& nodes = route.getNodes();
    const auto& states = route.getStates();
    
    auto customerNode = std::static_pointer_cast<Customer>(instance->getNodeById(nodeId));
    
    for (size_t pos = 1; pos < nodes.size(); ++pos) {
        int prev = nodes[pos - 1];
        
        // Distance detour
        double distBefore = instance->getDistance(prev, nodes[pos]);
        double distAfter = instance->getDistance(prev, nodeId) + instance->getDistance(nodeId, nodes[pos]);
        double detour = distAfter - distBefore;
        
        // Time window check
        double arrivalTime = states[pos - 1].departureTime + instance->getTime(prev, nodeId);
        
        double twPenalty = 0.0;
        if (arrivalTime > customerNode->getDueDate()) {
            twPenalty = 1000.0; // Infeasible penalty
        } else if (arrivalTime < customerNode->getReadyTime()) {
            twPenalty = 0.5; // Waiting penalty
        } else {
            double slack = customerNode->getDueDate() - arrivalTime;
            if (slack < 10.0) { // Arbitrary small slack value
                twPenalty = (10.0 - slack) / 10.0;
            }
        }
        
        double totalCost = detour * (1.0 + twPenalty);
        candidates.push_back({pos, totalCost});
    }
    
    // Use partial_sort for efficiency, as we only need the top K
    std::partial_sort(candidates.begin(), 
                     candidates.begin() + std::min(topK, (int)candidates.size()),
                     candidates.end());
    
    std::vector<size_t> result;
    for (int i = 0; i < std::min(topK, (int)candidates.size()); ++i) {
        // We can add a final feasibility check here if needed, but the heuristic cost should handle it
        result.push_back(candidates[i].position);
    }
    
    return result;
}

void LocalSearch::preprocessKNN() {
    auto customers = instance->getCustomers();
    
    for (const auto& customer : customers) {
        std::vector<std::pair<double, int>> distances;
        
        for (const auto& other : customers) {
            if (customer->getId() != other->getId()) {
                double dist = instance->getDistance(customer->getId(), other->getId());
                distances.push_back({dist, other->getId()});
            }
        }
        
        // Sort and take top K
        std::partial_sort(distances.begin(), 
                         distances.begin() + std::min(K_NEIGHBORS, (int)distances.size()),
                         distances.end());
        
        std::vector<int> nearestK;
        for (int i = 0; i < std::min(K_NEIGHBORS, (int)distances.size()); ++i) {
            nearestK.push_back(distances[i].second);
        }
        
        knnCache_[customer->getId()] = nearestK;
    }
}

void LocalSearch::preprocessGranularity() {
    auto customers = instance->getCustomers();
    int n = customers.size();
    
    if (n < 2) {
        avgDistance_ = 0.0;
        distanceThreshold_ = std::numeric_limits<double>::max();
        return;
    }
    
    // Calculate average distance between all customer pairs
    double totalDistance = 0.0;
    int pairCount = 0;
    
    for (size_t i = 0; i < customers.size(); ++i) {
        for (size_t j = i + 1; j < customers.size(); ++j) {
            totalDistance += instance->getDistance(
                customers[i]->getId(), 
                customers[j]->getId()
            );
            pairCount++;
        }
    }
    
    avgDistance_ = totalDistance / pairCount;
    distanceThreshold_ = GRANULARITY_FACTOR * avgDistance_;
}


std::vector<size_t> LocalSearch::findBestInsertionPositions_KNN(
    const Route& route, 
    int nodeId, 
    int topK
) const {
    // Get K nearest neighbors of nodeId
    auto it = knnCache_.find(nodeId);
    if (it == knnCache_.end()) {
        // Fallback to original method if node not in cache (should not happen for customers)
        return findBestInsertionPositions_TimeAware(route, nodeId, topK);
    }
    
    const auto& nearestNeighbors = it->second;
    const auto& nodes = route.getNodes();
    
    // Find positions adjacent to nearest neighbors present in the route
    std::set<size_t> candidatePositions;
    for (size_t pos = 0; pos < nodes.size(); ++pos) {
        int currentNodeId = nodes[pos];
        for (int neighborId : nearestNeighbors) {
            if (currentNodeId == neighborId) {
                // Add positions before and after this neighbor
                if (pos > 0) candidatePositions.insert(pos);
                if (pos < nodes.size()) candidatePositions.insert(pos + 1);
                break; // Move to next node in route
            }
        }
    }
    
    // If no neighbors found in route, fallback to checking all positions
    if (candidatePositions.empty()) {
        return findBestInsertionPositions_TimeAware(route, nodeId, topK);
    }
    
    // Evaluate only candidate positions and return top K
    struct Candidate {
        size_t position;
        double cost;
        bool operator<(const Candidate& o) const { return cost < o.cost; }
    };
    
    std::vector<Candidate> candidates;
    const auto& states = route.getStates();
    auto customerNode = std::static_pointer_cast<Customer>(instance->getNodeById(nodeId));
    
    for (size_t pos : candidatePositions) {
        if (pos == 0 || pos >= nodes.size()) continue; // Depot positions are invalid
        
        int prev = nodes[pos - 1];
        double distBefore = instance->getDistance(prev, nodes[pos]);
        double distAfter = instance->getDistance(prev, nodeId) + instance->getDistance(nodeId, nodes[pos]);
        double detour = distAfter - distBefore;
        
        // Time window penalty (same as TimeAware)
        double arrivalTime = states[pos - 1].departureTime + instance->getTime(prev, nodeId);
        double twPenalty = 0.0;
        if (arrivalTime > customerNode->getDueDate()) {
            twPenalty = 1000.0;
        } else if (arrivalTime < customerNode->getReadyTime()) {
            twPenalty = 0.5;
        }
        
        candidates.push_back({pos, detour * (1.0 + twPenalty)});
    }

    if (candidates.empty()) {
        return {}; // No valid insertion point found
    }
    
    std::partial_sort(candidates.begin(), 
                     candidates.begin() + std::min(topK, (int)candidates.size()),
                     candidates.end());
    
    std::vector<size_t> result;
    for (int i = 0; i < std::min(topK, (int)candidates.size()); ++i) {
        result.push_back(candidates[i].position);
    }
    
    return result;
}