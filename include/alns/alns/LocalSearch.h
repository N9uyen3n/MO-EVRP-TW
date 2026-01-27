#pragma once
#include "../core/Solution.h"
#include "../core/Instance.h"
#include <vector>
#include <memory>
#include <unordered_map>

// Định nghĩa các loại di chuyển
enum class MoveType {
    NONE,
    // Phase 1: Distance
    INTRA_RELOCATE, // Bao gồm cả Or-Opt (chuyển 1 node)
    INTRA_TWO_OPT,
    INTRA_SWAP,     // Swap 2 nodes trong cùng route
    INTRA_OR_OPT,   // Chuyển 1 segment trong cùng route
    INTER_RELOCATE,
    INTER_SWAP,
    INTER_TWO_OPT,
    INTER_OR_OPT,   // Chuyển 1 segment giữa 2 route

    // Phase 2: Charging
    STATION_INSERT,
    STATION_REMOVE,
    STATION_REMOVAL,  // Xóa trạm sạc không cần thiết
    STATION_SWAP,     // Thay thế trạm này bằng trạm khác

    // Phase 4: Vehicle Reduction
    ROUTE_MERGE // Cố gắng gộp 2 tuyến làm 1
};

struct MoveEvaluation {
    bool isFeasible = false;
    double distanceDelta = 0.0;
    double timeDelta = 0.0;
    double energyDelta = 0.0;
    double vehicleDelta = 0.0;
    double objectiveDelta = 0.0; // Weighted Sum dùng để so sánh nhanh

    void reset() {
        isFeasible = false;
        distanceDelta = timeDelta = energyDelta = vehicleDelta = objectiveDelta = 0.0;
    }
};

struct MoveDescriptor {
    MoveType type = MoveType::NONE;
    int routeIdx1 = -1;
    int routeIdx2 = -1;
    int nodeIdx1 = -1; // Vị trí nguồn
    int nodeIdx2 = -1; // Vị trí đích (hoặc nguồn 2)
    int segmentLength = 1; // For Or-Opt, defaults to 1 for relocate
    int stationId = -1; // Dùng cho charging moves

    MoveEvaluation eval;

    void reset() {
        type = MoveType::NONE;
        routeIdx1 = routeIdx2 = -1;
        nodeIdx1 = nodeIdx2 = -1;
        segmentLength = 1;
        stationId = -1;
        eval.reset();
    }
};

struct LocalSearchWeights {
    double dist = 1.0;
    double time = 0.0;
    double energy = 0.0;
    double vehicle = 0.0;
};

class LocalSearch {
public:
    explicit LocalSearch(std::shared_ptr<Instance> inst);

    // Hàm chạy chính
    void run(Solution& solution);

private:
    std::shared_ptr<Instance> instance;
    std::vector<int> stationIds; // Cache danh sách ID trạm sạc
    
    // ========== STATIC MOVE DESCRIPTOR ==========
    // Reused across all search operations to avoid repeated allocations
    MoveDescriptor activeMove;

    // ========== SEARCH CONTEXT CACHE ==========
    // Cache centroids and neighbor lists to avoid redundant calculations
    struct RouteCentroid { double x, y; };
    
    struct SearchContext {
        std::vector<RouteCentroid> centroids;
        std::vector<std::vector<int>> neighborLists;
        bool isValid = false;
        
        void invalidate() { isValid = false; }
    };
    
    SearchContext searchContext_;
    
    void updateSearchContext(const Solution& solution);

    // --- Core Logic ---
    void evaluateMove(const Solution& solution, MoveDescriptor& move, const LocalSearchWeights& weights);
    MoveEvaluation evaluateRelocateDelta(const Solution& solution, const MoveDescriptor& move);
    void applyMove(Solution& solution, const MoveDescriptor& move);

    // --- Phases ---
    bool runDistanceOptimization(Solution& solution);
    bool runChargingOptimization(Solution& solution);
    bool runLoadBalancing(Solution& solution);
    bool runVehicleReduction(Solution& solution);

    // --- Operators (Distance) ---
    bool searchRelocate(Solution& solution, MoveDescriptor& bestMove, const LocalSearchWeights& weights, const SearchContext& ctx);
    bool searchTwoOpt(Solution& solution, MoveDescriptor& bestMove, const LocalSearchWeights& weights);
    bool searchSwap(Solution& solution, MoveDescriptor& bestMove, const LocalSearchWeights& weights, const SearchContext& ctx);
    bool searchOrOpt(Solution& solution, MoveDescriptor& bestMove, const LocalSearchWeights& weights);

    // --- Operators (Charging) ---
    bool searchStationRemoval(Solution& solution, MoveDescriptor& bestMove, const LocalSearchWeights& weights);
    bool searchStationInsertion(Solution& solution, MoveDescriptor& bestMove, const LocalSearchWeights& weights);
    bool repositionStations(Solution& solution);
    bool optimizeChargingAmounts(Solution& solution);

    // --- Heuristics Helpers ---
    RouteCentroid computeCentroid(const Route& route) const;
    std::vector<RouteCentroid> computeAllCentroids(const Solution& solution);
    bool areRoutesClose(const RouteCentroid& c1, const RouteCentroid& c2, double threshold = 50.0) const;
    std::vector<std::pair<int, double>> rankNodesByRemovalSavings(const Route& route);
    std::vector<size_t> findBestInsertionPositions(const Route& route, int nodeId, int topK) const;
    std::vector<size_t> findBestInsertionPositions_TimeAware(const Route& route, int nodeId, int topK) const;
    std::vector<size_t> findBestInsertionPositions_Distance(const Route& route, int nodeId, int topK) const;
    std::vector<size_t> findBestInsertionPositions_KNN(const Route& route, int nodeId, int topK) const;
    
    // --- Vehicle Reduction Helpers ---
    bool mergeRoutes(Solution& solution, int targetRouteIdx, int sourceRouteIdx);


    // --- Adaptive Sizing Parameters ---
    int noImprovementCount_ = 0;
    int maxNodesToCheck_ = 5;    // For relocate
    int maxSwapAttempts_ = 3;    // For swap

    // --- Adaptive Sizing Constants ---
    static constexpr int MIN_NODES_TO_CHECK = 2;
    static constexpr int MAX_NODES_TO_CHECK = 10;
    static constexpr int MIN_SWAP_ATTEMPTS = 1;
    static constexpr int MAX_SWAP_ATTEMPTS = 5;

    // --- K-Nearest Neighbors Cache ---
    static constexpr int K_NEIGHBORS = 15;
    std::unordered_map<int, std::vector<int>> knnCache_;  // customerId -> list of K nearest customer IDs
    
    void preprocessKNN();  // Called in constructor

    // --- Granular Neighborhoods ---
    static constexpr double GRANULARITY_FACTOR = 1.5;
    double avgDistance_ = 0.0;
    double distanceThreshold_ = 0.0;
    
    void preprocessGranularity();  // Called in constructor

    // --- Operator Scoring Parameters ---
    struct OperatorStats {
        int attempts = 0;
        int successes = 0;
        double totalImprovement = 0.0;

        // Score = success_rate * avg_improvement
        double getScore() const {
            if (attempts == 0 || successes == 0) return 1.0; // Default score for exploration
            double successRate = static_cast<double>(successes) / attempts;
            double avgImprovement = totalImprovement / successes;
            return successRate * avgImprovement;
        }
    };

    OperatorStats relocateStats_;
    OperatorStats swapStats_;
    OperatorStats twoOptStats_;
    OperatorStats orOptStats_;
};