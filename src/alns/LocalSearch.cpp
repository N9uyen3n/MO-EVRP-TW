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
}

void LocalSearch::run(Solution& solution) {
    int maxIter = 50;
    int noImprove = 0;
    for (int iter = 0; iter < maxIter; ++iter) {
        bool improved = false;
        if (runDistanceOptimization(solution)) {
            improved = true;
        }
        if (runChargingOptimization(solution)) {
            improved = true;
        }
        if ((iter % 5 == 0 || !improved) && solution.getRoutes().size() > 1) {
            if (runVehicleReduction(solution)) {
                improved = true;
                noImprove = 0;
            }
        }
        if (improved) {
            solution.evaluateRoutes();
            noImprove = 0;
        } else {
            noImprove++;
            if (noImprove > 3) break;
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
            case MoveType::INTRA_TWO_OPT:
                r1_copy.reverseNodes(move.nodeIdx1, move.nodeIdx2);
                break;
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
    else if (move.type == MoveType::INTRA_TWO_OPT) {
        r1.reverseNodes(move.nodeIdx1, move.nodeIdx2);
        r1.evaluate();
    }
}

bool LocalSearch::runDistanceOptimization(Solution& solution) {
    LocalSearchWeights weights;
    weights.dist = 1.0; weights.time = 0.001;
    bool improved = false;
    MoveDescriptor bestMove;

    if (searchRelocate(solution, bestMove, weights)) {
        applyMove(solution, bestMove);
        improved = true;
    }
    bestMove.reset();
    if (searchTwoOpt(solution, bestMove, weights)) {
        applyMove(solution, bestMove);
        improved = true;
    }
    return improved;
}

bool LocalSearch::searchRelocate(Solution& solution, MoveDescriptor& outBestMove, const LocalSearchWeights& weights) {
    outBestMove.reset();
    outBestMove.eval.objectiveDelta = -1e-5;
    bool found = false;
    const auto& routes = solution.getRoutes();
    int numRoutes = routes.size();

    auto centroids = computeAllCentroids(solution);
    std::vector<std::vector<int>> neighborLists(numRoutes);
    for (int r1 = 0; r1 < numRoutes; ++r1) {
        for (int r2 = 0; r2 < numRoutes; ++r2) {
            if (r1 == r2 || areRoutesClose(centroids[r1], centroids[r2])) {
                neighborLists[r1].push_back(r2);
            }
        }
    }

    for (int r1 = 0; r1 < numRoutes; ++r1) {
        if (routes[r1].size() <= 2) continue;

        auto rankedNodes = rankNodesByRemovalSavings(routes[r1]);
        const int MAX_NODES_TO_CHECK = 10;
        int nodesToCheck = std::min(MAX_NODES_TO_CHECK, (int)rankedNodes.size());

        for (int k = 0; k < nodesToCheck; ++k) {
            int i = rankedNodes[k].first;
            int nodeId = routes[r1].getNodeAt(i);
            if (instance->getNodeById(nodeId)->getType() != NodeType::CUSTOMER) continue;

            for (int r2 : neighborLists[r1]) {
                
                // LAYER 4: Smart position selection
                auto candidatePositions = findBestInsertionPositions(routes[r2], nodeId, 5);

                // LAYER 5: Evaluate promising positions
                for (size_t j : candidatePositions) {
                    if (r1 == r2 && (j == i || j == i + 1)) continue;
                    
                    // Quick distance check is implicitly handled by findBestInsertionPositions
                    // Quick time window check is also handled by the TimeAware heuristic

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
    return false;
}
bool LocalSearch::searchSwap(Solution& solution, MoveDescriptor& outBestMove, const LocalSearchWeights& weights) {
    return false;
}
bool LocalSearch::runChargingOptimization(Solution& solution) {
    return false;
}
bool LocalSearch::searchStationRemoval(Solution& solution, MoveDescriptor& outBestMove, const LocalSearchWeights& weights) {
    return false;
}
bool LocalSearch::runVehicleReduction(Solution& solution) {
    return false;
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