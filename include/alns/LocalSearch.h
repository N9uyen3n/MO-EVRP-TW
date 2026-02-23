#pragma once
#include "../core/Instance.h"
#include "../core/Solution.h"
#include <memory>
#include <unordered_map>
#include <vector>

// Định nghĩa các loại di chuyển (chỉ giữ các type có operator thực sự)
enum class MoveType {
  NONE,
  // Phase 1: Distance Optimization
  INTRA_RELOCATE, // Dời 1 node trong cùng route
  INTRA_TWO_OPT,  // Đảo ngược đoạn trong cùng route
  INTRA_SWAP,     // Hoán đổi 2 nodes trong cùng route
  INTER_RELOCATE, // Dời 1 node sang route khác
  INTER_SWAP,     // Hoán đổi 2 nodes giữa 2 route
  INTER_OR_OPT,   // Dời 1 segment (2 nodes) sang route khác

  // Phase 2: Charging Optimization
  STATION_REMOVE, // Xóa trạm sạc thừa
  STATION_SWAP,   // Thay thế trạm bằng trạm tốt hơn

  // Phase 3: Vehicle Reduction
  ROUTE_MERGE // Gộp 2 tuyến thành 1
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
    distanceDelta = timeDelta = energyDelta = vehicleDelta = objectiveDelta =
        0.0;
  }
};

struct MoveDescriptor {
  MoveType type = MoveType::NONE;
  int routeIdx1 = -1;
  int routeIdx2 = -1;
  int nodeIdx1 = -1;     // Vị trí nguồn
  int nodeIdx2 = -1;     // Vị trí đích (hoặc nguồn 2)
  int segmentLength = 1; // For Or-Opt, defaults to 1 for relocate
  int stationId = -1;    // Dùng cho charging moves

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

// Energy Boost Result Structure (for Partial Charging strategy)
struct EnergyBoostResult {
  bool success = false;
  int stationPosition = -1; // Position of station to boost
  double extraCharge = 0.0; // Amount of extra energy needed
  double addedTime = 0.0;   // Time cost of extra charging
};

class LocalSearch {
public:
  explicit LocalSearch(std::shared_ptr<Instance> inst);

  // Hàm chạy chính
  void run(Solution &solution);

private:
  std::shared_ptr<Instance> instance;
  std::vector<int> stationIds; // Cache danh sách ID trạm sạc

  // ========== STATIC MOVE DESCRIPTOR ==========
  // Reused across all search operations to avoid repeated allocations
  MoveDescriptor activeMove;

  // ========== SEARCH CONTEXT CACHE ==========
  // Cache centroids and neighbor lists to avoid redundant calculations
  struct RouteCentroid {
    double x, y;
  };

  struct SearchContext {
    std::vector<RouteCentroid> centroids;
    std::vector<std::vector<int>> neighborLists;
    bool isValid = false;

    void invalidate() { isValid = false; }
  };

  SearchContext searchContext_;

  void updateSearchContext(const Solution &solution);

  // --- Core Logic ---
  void evaluateMove(const Solution &solution, MoveDescriptor &move,
                    const LocalSearchWeights &weights);
  MoveEvaluation evaluateRelocateDelta(const Solution &solution,
                                       const MoveDescriptor &move);
  void applyMove(Solution &solution, const MoveDescriptor &move);

  // --- Phases (3-Phase Local Search) ---
  bool runDistanceOptimization(
      Solution &solution); // Phase 1: VND over {Relocate, Swap, Or-Opt, 2-Opt}
  bool runChargingOptimization(
      Solution &solution); // Phase 2: Station cleanup & repositioning
  bool runVehicleReduction(
      Solution &solution); // Phase 3: Route merging & elimination

  // --- Operators (Distance) ---
  bool searchRelocate(Solution &solution, MoveDescriptor &bestMove,
                      const LocalSearchWeights &weights,
                      const SearchContext &ctx);
  bool searchTwoOpt(Solution &solution, MoveDescriptor &bestMove,
                    const LocalSearchWeights &weights);
  bool searchSwap(Solution &solution, MoveDescriptor &bestMove,
                  const LocalSearchWeights &weights, const SearchContext &ctx);
  bool searchOrOpt(Solution &solution, MoveDescriptor &bestMove,
                   const LocalSearchWeights &weights);

  // --- Operators (Charging) ---
  bool searchStationRemoval(Solution &solution, MoveDescriptor &bestMove,
                            const LocalSearchWeights &weights);
  bool searchStationInsertion(Solution &solution, MoveDescriptor &bestMove,
                              const LocalSearchWeights &weights);
  bool repositionStations(Solution &solution);
  bool
  searchStationSwap(Solution &solution); // ⭐ NEW: Replace suboptimal stations
                                         // with centroid-optimal ones
  bool optimizeChargingAmounts(Solution &solution);
  int removeRedundantStations(Route &route); // ⭐ NEW: Greedy station cleanup
  bool removeRedundantStations(Solution &solution); // Wrapper for all routes

  // --- Heuristics Helpers ---
  RouteCentroid computeCentroid(const Route &route) const;
  std::vector<RouteCentroid> computeAllCentroids(const Solution &solution);
  bool areRoutesClose(const RouteCentroid &c1, const RouteCentroid &c2,
                      double threshold = 50.0) const;
  std::vector<std::pair<int, double>>
  rankNodesByRemovalSavings(const Route &route);
  std::vector<size_t> findBestInsertionPositions(const Route &route, int nodeId,
                                                 int topK) const;
  std::vector<size_t> findBestInsertionPositions_TimeAware(const Route &route,
                                                           int nodeId,
                                                           int topK) const;
  std::vector<size_t> findBestInsertionPositions_Distance(const Route &route,
                                                          int nodeId,
                                                          int topK) const;
  std::vector<size_t> findBestInsertionPositions_KNN(const Route &route,
                                                     int nodeId,
                                                     int topK) const;

  // --- Vehicle Reduction Helpers ---
  bool runSmartMultiRouteMerge(Solution &solution);
  bool tryEliminateSmallestRoute(Solution &solution);

  int findNearestStation(int nodeId) const;

  // --- Energy Boost Helpers (Partial Charging Strategy) ---
  int findPrecedingStation(const Route &route, size_t insertPos) const;
  double calculateEnergyGap(const Route &route, size_t position) const;
  bool tryRelocateWithEnergyBoost(Route &routeFrom, int fromNodeIdx,
                                  Route &routeTo, size_t insertPos,
                                  double maxExtraTimeAllowed = 30.0);

  // --- Helpers for SmartMerge ---

  std::vector<size_t> getTopKInsertionPositions(const Route &route, int nodeId,
                                                int topK) const;
  double calculateEuclideanDistance(const RouteCentroid &c1,
                                    const RouteCentroid &c2) const;

  // ========== ITERATION CONTROL CONSTANTS ==========
  static constexpr int MAX_LS_ITERATIONS = 10; // Tổng số vòng lặp tối đa (reduced to prevent LS dominance)
  static constexpr int EARLY_STOP_THRESHOLD = 3; // Dừng sớm sau N vòng không cải tiến
  static constexpr int CHARGING_FREQUENCY = 2; // Chạy Phase 2 mỗi N vòng
  static constexpr int VEHICLE_REDUCTION_FREQUENCY = 3; // Chạy Phase 3 mỗi N vòng

  // --- Adaptive Sizing Parameters ---
  int noImprovementCount_ = 0;
  int maxNodesToCheck_ = 5; // For relocate
  int maxSwapAttempts_ = 3; // For swap

  // --- Adaptive Sizing Constants ---
  static constexpr int MIN_NODES_TO_CHECK = 2;
  static constexpr int MAX_NODES_TO_CHECK = 10;
  static constexpr int MIN_SWAP_ATTEMPTS = 1;
  static constexpr int MAX_SWAP_ATTEMPTS = 5;

  // --- K-Nearest Neighbors Cache ---
  static constexpr int K_NEIGHBORS = 15;
  std::unordered_map<int, std::vector<int>>
      knnCache_; // customerId -> list of K nearest customer IDs

  void preprocessKNN(); // Called in constructor

  // --- Granular Neighborhoods ---
  static constexpr double GRANULARITY_FACTOR = 1.5;
  double avgDistance_ = 0.0;
  double distanceThreshold_ = 0.0;

  void preprocessGranularity(); // Called in constructor
};