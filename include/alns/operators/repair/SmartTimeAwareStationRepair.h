#pragma once
#include "../../../core/Customer.h"
#include "../../../core/Instance.h"
#include "../../../core/Solution.h"
#include "../../../core/Station.h"
#include "../../IOperator.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <random>
#include <vector>


namespace alns {

/**
 * @brief Smart Time-Aware Station Repair v2.0
 *
 * An upgraded repair operator that:
 * 1. Tries direct (no-station) insertion first.
 * 2. Uses O(1) Delta Calculation for station evaluation (avoid Deep Copy +
 * evaluate).
 * 3. Searches KNN stations based on edge midpoint for better coverage.
 * 4. Detects "Free Charge" opportunities (charging during slack time).
 * 5. Uses normalized scoring (W_energy / W_time) for balanced station
 * selection.
 *
 * Runs in PARALLEL with SmartStationRepair. ALNS adaptive weights
 * will naturally favor the more effective operator over time.
 */
class SmartTimeAwareStationRepair : public IRepairOperator {
public:
  explicit SmartTimeAwareStationRepair(std::shared_ptr<Instance> instance);

  std::string getName() const override;

  void execute(Solution &solution, const std::vector<int> &unservedCustomers,
               std::mt19937 &rng) override;

private:
  std::shared_ptr<Instance> instance;

  // --- Structs ---
  struct StationMoveResult {
    bool isValid = false;
    bool isFreeCharge = false;
    double energyNeeded = 0.0;
    double detourDist = 0.0;
    double detourTime = 0.0;
    double totalChargeTime = 0.0;
    int stationId = -1;
  };

  struct InsertionCandidate {
    int customerId = -1;
    int routeIndex = -1;
    int position = -1;
    double costIncrease = std::numeric_limits<double>::infinity();

    bool requiresStation = false;
    int stationId = -1;
    int stationPosition = -1; // Position to insert station
  };

  // --- Core Methods ---
  InsertionCandidate findBestInsertion(int customerId, Solution &solution);

  bool tryInsertWithoutStation(const Route &route, int customerId, size_t pos,
                               int routeIndex, InsertionCandidate &best);

  void tryInsertWithStation(const Route &route, int customerId, size_t pos,
                            int routeIndex, InsertionCandidate &best);

  StationMoveResult evaluateStationMove(const Route &routeWithCust,
                                        int stationId, size_t uPos,
                                        double battAtU, double departU);

  double calculateStationScore(const StationMoveResult &res);

  // --- Helpers ---
  std::vector<int> findKNearestStations(double midX, double midY, int k);
};

} // namespace alns
