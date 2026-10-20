#pragma once
#include "core/Route.h"
#include <chrono>
#include <optional>

namespace alns {
struct ChargingRepairBudget {
  int maxEvaluations = 80;
  int maxStations = 6; // Visits, including repeated visits to the same station.
  int stationCandidates = 3;
  int beamWidth = 8;
  std::chrono::milliseconds maxDuration{3};
  std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max();
};

enum class ChargingRepairStatus { SUCCESS, NO_CANDIDATE, BUDGET_EXHAUSTED, INVALID_INPUT };

struct ChargingRepairResult {
  ChargingRepairStatus status = ChargingRepairStatus::NO_CANDIDATE;
  std::optional<Route> route;
  int evaluations = 0;
  long long elapsedUs = 0;
  bool budgetExhausted = false;
};

// The ordered customer list is immutable. Failure means this bounded search did
// not find a schedule; it is not a proof that no feasible charging plan exists.
ChargingRepairResult repairChargingForCustomerSequence(
    const Route& original, const std::shared_ptr<Instance>& instance,
    const std::vector<int>& orderedCustomers, const ChargingRepairBudget& budget);

struct ChargingRepairStatistics {
  long long calls = 0;
  long long feasible = 0;
  long long exhausted = 0;
  long long evaluations = 0;
  long long elapsedUs = 0;
  long long relocateAccepted = 0;
  long long interTwoOptAccepted = 0;
  long long vehiclesReduced = 0;
};
} // namespace alns
