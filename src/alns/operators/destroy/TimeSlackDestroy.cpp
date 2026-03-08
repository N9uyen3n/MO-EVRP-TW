#include "../../../../include/alns/operators/destroy/TimeSlackDestroy.h"

#include "core/Customer.h"
#include "core/Route.h"
#include "core/Solution.h"
#include "core/Station.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace alns {

// ============================================================================
// Constructor
// ============================================================================

TimeSlackDestroy::TimeSlackDestroy(std::shared_ptr<Instance> instance,
                                   double explorationFactor)
    : instance_(std::move(instance)), explorationFactor_(explorationFactor) {}

// ============================================================================
// computeTimingData
// ============================================================================

TimeSlackDestroy::RouteTimingData
TimeSlackDestroy::computeTimingData(const Route &route) const {
  const auto &nodes = route.getNodes();
  const auto &states = route.getStates();
  const int n = static_cast<int>(nodes.size());

  RouteTimingData data;
  data.arrivalTime.resize(n, 0.0);
  data.serviceStart.resize(n, 0.0);
  data.slack.resize(n, 0.0);
  data.fts.resize(n, std::numeric_limits<double>::max());

  if (n < 2 || states.empty())
    return data;

  // ── Forward pass ──────────────────────────────────────────────────────
  for (int i = 1; i < n; ++i) {
    auto node = instance_->getNodeById(nodes[i]);
    data.arrivalTime[i] = states[i - 1].departureTime +
                          instance_->getTime(nodes[i - 1], nodes[i]);
    data.serviceStart[i] = std::max(data.arrivalTime[i], node->getReadyTime());

    if (node->getType() == NodeType::CUSTOMER) {
      data.slack[i] = node->getDueDate() - data.serviceStart[i];
      data.slack[i] = std::max(0.0, data.slack[i]);
      data.custPositions.push_back(i);
    }
  }

  // ── Backward pass: FTS với station-aware reset ────────────────────────
  const int K = static_cast<int>(data.custPositions.size());
  if (K == 0)
    return data;

  data.fts[data.custPositions[K - 1]] = data.slack[data.custPositions[K - 1]];

  for (int k = K - 2; k >= 0; --k) {
    const int pos_k = data.custPositions[k];
    const int pos_next = data.custPositions[k + 1];

    bool hasStation = false;
    for (int m = pos_k + 1; m < pos_next; ++m) {
      if (instance_->getNodeById(nodes[m])->getType() == NodeType::STATION) {
        hasStation = true;
        break;
      }
    }

    if (hasStation) {
      data.fts[pos_k] = data.slack[pos_k];
    } else {
      data.fts[pos_k] = std::min(data.slack[pos_k], data.fts[pos_next]);
    }
  }

  return data;
}

// ============================================================================
// computeLiberationScore
// ============================================================================

double TimeSlackDestroy::computeLiberationScore(const RouteTimingData &data,
                                                int k,
                                                const Route &route) const {
  const auto &nodes = route.getNodes();
  const auto &states = route.getStates();
  const auto &custPos = data.custPositions;
  const int pos_i = custPos[k];
  auto node_i = instance_->getNodeById(nodes[pos_i]);

  // ── Thành phần A: FTS Criticality ──────────────────────────────────────
  int lockCount = 0;
  for (int j = k - 1; j >= 0; --j) {
    const int pos_j = custPos[j];

    bool hasStation = false;
    for (int m = pos_j + 1; m < pos_i; ++m) {
      if (instance_->getNodeById(nodes[m])->getType() == NodeType::STATION) {
        hasStation = true;
        break;
      }
    }
    if (hasStation)
      break;

    if (std::abs(data.fts[pos_j] - data.fts[pos_i]) < EPSILON) {
      ++lockCount;
    }
  }

  const double ftsCriticalityScore =
      (1.0 / (data.fts[pos_i] + EPSILON)) * (1.0 + lockCount);

  // ── Thành phần B: Actual Time Saved ────────────────────────────────────
  const int pos_prev = pos_i - 1;
  const int pos_next = pos_i + 1;

  double actualTimeSaved = 0.0;
  if (pos_next < static_cast<int>(nodes.size()) && pos_prev >= 0 &&
      static_cast<int>(states.size()) > pos_next) {

    auto node_next = instance_->getNodeById(nodes[pos_next]);

    const double currentDeparture_next = states[pos_next].departureTime;

    const double newArrival_next =
        states[pos_prev].departureTime +
        instance_->getTime(nodes[pos_prev], nodes[pos_next]);

    double newDeparture_next;
    if (node_next->getType() == NodeType::CUSTOMER) {
      newDeparture_next = std::max(newArrival_next, node_next->getReadyTime()) +
                          node_next->getServiceTime();
    } else {
      newDeparture_next = newArrival_next;
    }

    actualTimeSaved = std::max(0.0, currentDeparture_next - newDeparture_next);
  }

  // ── Thành phần C: Waiting time tại chính C_i ───────────────────────────
  const double waitAtI =
      std::max(0.0, node_i->getReadyTime() - data.arrivalTime[pos_i]);

  // ── Tổng hợp ───────────────────────────────────────────────────────────
  return ALPHA * ftsCriticalityScore + BETA * actualTimeSaved + GAMMA * waitAtI;
}

// ============================================================================
// execute — Main operator logic
// ============================================================================

std::vector<int> TimeSlackDestroy::execute(Solution &solution, int nRemove,
                                           std::mt19937 &rng) {
  std::vector<int> unservedList;
  if (nRemove <= 0)
    return unservedList;

  auto &routes = solution.getRoutes();
  const int numRoutes = static_cast<int>(routes.size());
  if (numRoutes == 0)
    return unservedList;

  std::vector<RouteTimingData> timingCache(numRoutes);
  std::unordered_map<int, CandidateEntry> candidateScores;
  candidateScores.reserve(100);

  for (int r = 0; r < numRoutes; ++r) {
    timingCache[r] = computeTimingData(routes[r]);
    const auto &data = timingCache[r];
    const auto &custPos = data.custPositions;

    for (int k = 0; k < static_cast<int>(custPos.size()); ++k) {
      const int custId = routes[r].getNodeAt(custPos[k]);
      const double score = computeLiberationScore(data, k, routes[r]);
      candidateScores[custId] = {score, r, custPos[k]};
    }
  }

  std::uniform_real_distribution<double> uniform(0.0, 1.0);

  while (static_cast<int>(unservedList.size()) < nRemove &&
         !candidateScores.empty()) {

    const int K = std::min(TOP_K, static_cast<int>(candidateScores.size()));

    struct ScoredCand {
      int    custId;
      double score;
      int    routeIdx;
      int    posInRoute;
    };
    std::vector<ScoredCand> allCands;
    allCands.reserve(candidateScores.size());
    for (const auto &kv : candidateScores) {
      allCands.push_back({kv.first, kv.second.score, kv.second.routeIdx, kv.second.posInRoute});
    }

    std::partial_sort(allCands.begin(), allCands.begin() + K, allCands.end(),
                      [](const ScoredCand &a, const ScoredCand &b) {
                        return a.score > b.score;
                      });

    const double randVal = uniform(rng);
    const double expVal = std::pow(randVal, explorationFactor_);
    const int idx = std::min(K - 1, static_cast<int>(std::floor(expVal * K)));

    const int selectedCustId = allCands[idx].custId;
    const int routeIdx       = allCands[idx].routeIdx;
    const int posInRoute     = allCands[idx].posInRoute;

    Route &route = routes[routeIdx];

    route.removeNode(posInRoute);
    unservedList.push_back(selectedCustId);
    candidateScores.erase(selectedCustId);

    const auto &routeCustomers = route.getCustomers();
    if (routeCustomers.empty()) {
      solution.removeRoute(routeIdx);
      candidateScores.clear();
      const auto &updatedRoutes = solution.getRoutes();
      const int newNumRoutes = static_cast<int>(updatedRoutes.size());
      timingCache.resize(newNumRoutes);
      for (int rr = 0; rr < newNumRoutes; ++rr) {
        timingCache[rr] = computeTimingData(updatedRoutes[rr]);
        const auto &data2 = timingCache[rr];
      for (int k = 0; k < static_cast<int>(data2.custPositions.size()); ++k) {
          const int cid = updatedRoutes[rr].getNodeAt(data2.custPositions[k]);
          candidateScores[cid] = {
              computeLiberationScore(data2, k, updatedRoutes[rr]), rr, data2.custPositions[k]};
        }
      }
    } else {
      route.evaluate();
      timingCache[routeIdx] = computeTimingData(route);
      const auto &newData = timingCache[routeIdx];
      const auto &newCustPos = newData.custPositions;

      for (int k = 0; k < static_cast<int>(newCustPos.size()); ++k) {
        const int cid = route.getNodeAt(newCustPos[k]);
        if (candidateScores.count(cid)) {
          candidateScores[cid].score =
              computeLiberationScore(newData, k, route);
          candidateScores[cid].posInRoute = newCustPos[k];
        }
      }
    }
  }

  return unservedList;
}

} // namespace alns
