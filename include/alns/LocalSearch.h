#pragma once
#include "../core/Instance.h"
#include "../core/Solution.h"
#include <memory>
#include <random>
#include <unordered_map>
#include <vector>

enum class MoveType {
  NONE,
  INTRA_RELOCATE,
  INTRA_TWO_OPT,
  INTRA_SWAP,
  INTER_RELOCATE,
  INTER_SWAP,
  INTER_OR_OPT,
  INTER_TWO_OPT,
  INTER_CROSS_EXCHANGE,
  STATION_REMOVE,
  STATION_SWAP,
  ROUTE_MERGE
};

struct MoveEvaluation {
  bool isFeasible = false;
  double distanceDelta = 0.0;
  double timeDelta = 0.0;
  double energyDelta = 0.0;
  double vehicleDelta = 0.0;
  double objectiveDelta = 0.0;

  void reset() {
    isFeasible = false;
    distanceDelta = timeDelta = energyDelta = vehicleDelta = objectiveDelta = 0.0;
  }
};

struct MoveDescriptor {
  MoveType type = MoveType::NONE;
  int routeIdx1 = -1;
  int routeIdx2 = -1;
  int nodeIdx1 = -1;
  int nodeIdx2 = -1;
  int segmentLength = 1;
  int segmentLength2 = 1;
  int stationId = -1;
  double cachedRemovalSavings = 0.0;

  MoveEvaluation eval;

  void reset() {
    type = MoveType::NONE;
    routeIdx1 = routeIdx2 = -1;
    nodeIdx1 = nodeIdx2 = -1;
    segmentLength = 1;
    segmentLength2 = 1;
    stationId = -1;
    cachedRemovalSavings = 0.0;
    eval.reset();
  }
};

struct LocalSearchWeights {
  double dist = 1.0;
  double time = 0.0;
  double energy = 0.0;
  double vehicle = 0.0;
};

struct EnergyBoostResult {
  bool success = false;
  int stationPosition = -1;
  double extraCharge = 0.0;
  double addedTime = 0.0;
};

class LocalSearch {
public:
  explicit LocalSearch(std::shared_ptr<Instance> inst);
  void run(Solution &solution);
  bool reconstructRouteWithTwoStations(Route &route,
  const std::vector<int> &newCustomers, int maxStations = 2) const;
  // bool forceReduceVehicles(Solution &solution, int targetVehicles,
  //     int maxAttempts, std::mt19937 &rng) const;
  int getVehicleReductionFeq();
  void setVehicleReductionFeq(int number);

private:
  std::shared_ptr<Instance> instance;
  std::vector<int> stationIds;

  // [CHANGE-5] activeMove removed — each operator now uses a local
  // MoveDescriptor to avoid shared mutable state between operators.

  // ========== SEARCH CONTEXT CACHE ==========
  struct RouteCentroid { double x, y; };

  struct SearchContext {
    std::vector<RouteCentroid> centroids;
    std::vector<std::vector<int>> neighborLists;
    bool isValid = false;
    std::vector<std::vector<std::pair<int, double>>> removalRankings;
    std::vector<bool> rankingDirty;
    std::vector<bool> centroidDirty;  // [SMD] track routes cần recompute centroid+neighborList

    void invalidate() {
      isValid = false;
      // centroidDirty sẽ được assign lại trong updateSearchContext() khi rebuild
    }

    void markDirty(int r1, int r2 = -1) {
      if (r1 >= 0 && r1 < (int)rankingDirty.size()) {
        rankingDirty[r1]  = true;
        centroidDirty[r1] = true;  // [SMD]
      }
      if (r2 >= 0 && r2 < (int)rankingDirty.size()) {
        rankingDirty[r2]  = true;
        centroidDirty[r2] = true;  // [SMD]
      }
    }
  };

  SearchContext searchContext_;
  void updateSearchContext(const Solution &solution);

  // --- Core Logic ---
  const std::vector<std::pair<int, double>> &
  getCachedRanking(int routeIdx, const Route &route, SearchContext &ctx);
  void evaluateMove(const Solution &solution, MoveDescriptor &move,
                    const LocalSearchWeights &weights);
  MoveEvaluation evaluateRelocateDelta(const Solution &solution,
                                       const MoveDescriptor &move);
  void applyMove(Solution &solution, const MoveDescriptor &move);

  // --- Phases ---
  bool runDistanceOptimization(Solution &solution);  // Phase 1: VND
  bool runChargingOptimization(Solution &solution);  // Phase 2: Station cleanup
  bool runVehicleReduction(Solution &solution);      // Phase 3: Merged reduction

  // --- Operators (Distance) ---
  // [CHANGE-3/4] outBestMove param removed — first-improvement, apply immediately
  bool searchRelocate(Solution &solution,
                      const LocalSearchWeights &weights, SearchContext &ctx);
  bool searchTwoOpt(Solution &solution,
                    const LocalSearchWeights &weights);
  bool searchInterTwoOpt(Solution &solution,
                         const LocalSearchWeights &weights, SearchContext &ctx);
  bool searchSwap(Solution &solution,
                  const LocalSearchWeights &weights, SearchContext &ctx);
  bool searchOrOpt(Solution &solution,
                   const LocalSearchWeights &weights);
  bool searchCrossExchange(Solution &solution,
                           const LocalSearchWeights &weights, SearchContext &ctx);
  bool searchIntraOrOpt(Solution &solution, const LocalSearchWeights &weights, SearchContext &ctx);
  bool searchOrOptReversed(Solution &solution, const LocalSearchWeights &weights, SearchContext &ctx);
  void patchSearchContext(const Solution &solution);

  // --- Operators (Charging) ---
  bool searchStationRemoval(Solution &solution, MoveDescriptor &bestMove,
                            const LocalSearchWeights &weights);
  bool searchStationInsertion(Solution &solution, MoveDescriptor &bestMove,
                              const LocalSearchWeights &weights);
  bool repositionStations(Solution &solution);
  bool searchStationSwap(Solution &solution);
  bool optimizeChargingAmounts(Solution &solution);
  int  removeRedundantStations(Route &route);
  bool removeRedundantStations(Solution &solution);
  bool searchStationInsertion(Solution &solution);

  // --- Vehicle Reduction ---
  // [CHANGE-2] runElectricityFreeVehicleReduction merged into runSmartMultiRouteMerge
  bool runSmartMultiRouteMerge(Solution &solution);
  bool tryEliminateSmallestRoute(Solution &solution);
  bool ejectionChain(Solution &solution);
  bool segmentCrossExchangeForVehicleReduction(Solution &solution);

  // --- Energy Boost Helpers ---
  int    findNearestStation(int nodeId) const;
  int    findPrecedingStation(const Route &route, size_t insertPos) const;
  double calculateEnergyGap(const Route &route, size_t position) const;
  bool   tryRelocateWithEnergyBoost(Route &routeFrom, int fromNodeIdx,
                                    Route &routeTo, size_t insertPos,
                                    double maxExtraTimeAllowed = 30.0);

  // --- Insertion Helpers ---
  std::vector<size_t> findBestInsertionPositions(const Route &route,
                                                  int nodeId, int topK) const;
  std::vector<size_t> findBestInsertionPositions_TimeAware(const Route &route,
                                                            int nodeId, int topK) const;
  std::vector<size_t> findBestInsertionPositions_Distance(const Route &route,
                                                           int nodeId, int topK) const;
  std::vector<size_t> findBestInsertionPositions_KNN(const Route &route,
                                                      int nodeId, int topK) const;
  std::vector<size_t> getTopKInsertionPositions(const Route &route,
                                                 int nodeId, int topK) const;

  // --- Geometry Helpers ---
  RouteCentroid computeCentroid(const Route &route) const;
  std::vector<RouteCentroid> computeAllCentroids(const Solution &solution);
  bool areRoutesClose(const RouteCentroid &c1, const RouteCentroid &c2,
                      double threshold = 50.0) const;
  double calculateEuclideanDistance(const RouteCentroid &c1,
                                    const RouteCentroid &c2) const;
  std::vector<std::pair<int, double>> rankNodesByRemovalSavings(const Route &route);

  // ========== CONSTANTS ==========
  static constexpr int    MAX_LS_ITERATIONS      = 20;
  static constexpr int    EARLY_STOP_THRESHOLD   = 7;
  static constexpr int    CHARGING_FREQUENCY     = 3;
  static constexpr int    MIN_NODES_TO_CHECK     = 3;
  static constexpr int    MAX_NODES_TO_CHECK     = 10;
  static constexpr int    MIN_SWAP_ATTEMPTS      = 2;
  static constexpr int    MAX_SWAP_ATTEMPTS      = 10;
  static constexpr int    K_NEIGHBORS            = 40;
  static constexpr double GRANULARITY_FACTOR     = 1.5;

  // --- Adaptive Sizing ---
  int noImprovementCount_ = 0;
  int maxNodesToCheck_    = 10;
  int maxSwapAttempts_    = 15;
  mutable int vehicleReductionFreq = 1;

  // --- KNN Cache ---
  std::unordered_map<int, std::vector<int>> knnCache_;
  void preprocessKNN();

  // --- Granular Neighborhoods ---
  double avgDistance_       = 0.0;
  double distanceThreshold_ = 0.0;
  void preprocessGranularity();

  // --- TW Pruning ---
  std::vector<std::vector<bool>> twNext_;
  void preprocessTWNext();

  // --- Node Caches ---
  std::vector<NodeType> nodeTypeById_;
  std::vector<double>   demandById_;
};