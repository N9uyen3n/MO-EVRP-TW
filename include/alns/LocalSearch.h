#pragma once
#include "../core/Solution.h"
#include "../core/Instance.h"
#include <vector>
#include <memory>

// Định nghĩa các loại di chuyển
enum class MoveType {
    NONE,
    // Phase 1: Distance
    INTRA_RELOCATE, // Bao gồm cả Or-Opt (chuyển 1 node)
    INTRA_TWO_OPT,
    INTER_RELOCATE,
    INTER_SWAP,
    INTER_TWO_OPT,

    // Phase 2: Charging
    STATION_INSERT,
    STATION_REMOVE,
    STATION_SWAP, // Thay thế trạm này bằng trạm khác

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
    int stationId = -1; // Dùng cho charging moves

    MoveEvaluation eval;

    void reset() {
        type = MoveType::NONE;
        routeIdx1 = routeIdx2 = -1;
        nodeIdx1 = nodeIdx2 = -1;
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

    // --- Core Logic ---
    void evaluateMove(const Solution& solution, MoveDescriptor& move, const LocalSearchWeights& weights);
    void applyMove(Solution& solution, const MoveDescriptor& move);

    // --- Phases ---
    bool runDistanceOptimization(Solution& solution);
    bool runChargingOptimization(Solution& solution);
    bool runLoadBalancing(Solution& solution);
    bool runVehicleReduction(Solution& solution);

    // --- Operators (Distance) ---
    bool searchRelocate(Solution& solution, MoveDescriptor& bestMove, const LocalSearchWeights& weights);
    bool searchTwoOpt(Solution& solution, MoveDescriptor& bestMove, const LocalSearchWeights& weights);
    bool searchSwap(Solution& solution, MoveDescriptor& bestMove, const LocalSearchWeights& weights);

    // --- Operators (Charging) ---
    bool searchStationRemoval(Solution& solution, MoveDescriptor& bestMove, const LocalSearchWeights& weights);
    bool searchStationInsertion(Solution& solution, MoveDescriptor& bestMove, const LocalSearchWeights& weights);

    // --- Heuristics Helpers ---
    struct RouteCentroid { double x, y; };
    RouteCentroid computeCentroid(const Route& route) const;
    bool areRoutesClose(const Route& r1, const Route& r2, double threshold = 50.0) const;
    std::vector<int> getBoundaryNodes(const Route& route) const; // Lấy node đầu/cuối để optimize
};