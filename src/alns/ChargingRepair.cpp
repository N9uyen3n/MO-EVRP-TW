#include "alns/ChargingRepair.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <tuple>

namespace alns {
namespace {
struct Candidate {
  std::vector<int> nodes;
  int visits = 0;
  double excess = 0;
  double distance = 0;
};

Candidate describe(std::vector<int> nodes, const Instance& instance, double battery, double rate) {
  Candidate candidate;
  candidate.nodes = std::move(nodes);
  double segment = 0;
  for (size_t i = 1; i < candidate.nodes.size(); ++i) {
    const double distance = instance.getDistance(candidate.nodes[i - 1], candidate.nodes[i]);
    candidate.distance += distance;
    segment += distance * rate;
    if (instance.getNodeType(candidate.nodes[i]) != NodeType::CUSTOMER) {
      candidate.excess += std::max(0.0, segment - battery);
      segment = 0;
      if (instance.getNodeType(candidate.nodes[i]) == NodeType::STATION) ++candidate.visits;
    }
  }
  return candidate;
}

bool better(const Candidate& a, const Candidate& b) {
  return std::tie(a.excess, a.distance, a.visits, a.nodes) <
         std::tie(b.excess, b.distance, b.visits, b.nodes);
}
} // namespace

ChargingRepairResult repairChargingForCustomerSequence(
    const Route& original, const std::shared_ptr<Instance>& instance,
    const std::vector<int>& customers, const ChargingRepairBudget& budget) {
  using Clock = std::chrono::steady_clock;
  const auto start = Clock::now();
  const auto deadline = std::min(budget.deadline, start + budget.maxDuration);
  ChargingRepairResult result;
  auto finish = [&]() {
    result.elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
    if (result.route) result.status = ChargingRepairStatus::SUCCESS;
    else if (result.budgetExhausted) result.status = ChargingRepairStatus::BUDGET_EXHAUSTED;
    return result;
  };
  if (!instance || budget.maxEvaluations < 0 || budget.maxStations < 0 ||
      budget.beamWidth <= 0 || budget.stationCandidates < 0 || budget.maxDuration.count() < 0) {
    result.status = ChargingRepairStatus::INVALID_INPUT;
    return finish();
  }
  const auto vehicle = original.getVehicle();
  double demand = 0;
  std::set<int> unique;
  for (int id : customers) {
    if (id < 0 || static_cast<size_t>(id) >= instance->getNodes().size() ||
        instance->getNodeType(id) != NodeType::CUSTOMER || !unique.insert(id).second) {
      result.status = ChargingRepairStatus::INVALID_INPUT;
      return finish();
    }
    demand += instance->getNodeRaw(id)->getDemand();
  }
  if (demand > vehicle->getCapacity() + 1e-9) return finish();
  auto exhausted = [&]() {
    if (result.evaluations >= budget.maxEvaluations || Clock::now() >= deadline) {
      result.budgetExhausted = true;
      return true;
    }
    return false;
  };
  std::vector<int> skeleton{0};
  skeleton.insert(skeleton.end(), customers.begin(), customers.end());
  skeleton.push_back(0);
  const double battery = vehicle->getBatteryCapacity();
  const double rate = vehicle->getEnergyConsumptionRate();
  std::vector<Candidate> beam;
  beam.push_back(describe(skeleton, *instance, battery, rate));
  if (original.getCustomers() == customers && original.getNodes() != skeleton) {
    auto preserved = describe(original.getNodes(), *instance, battery, rate);
    if (preserved.visits <= budget.maxStations) beam.push_back(std::move(preserved));
  }
  const auto seeds = beam;
  const int stationCount = static_cast<int>(instance->getStations().size());
  int candidateCount = std::min(stationCount, budget.stationCandidates);
  const size_t generationLimit = static_cast<size_t>(std::max(1, budget.maxEvaluations)) * 32;
  size_t generated = 0;

  for (;;) {
    beam = seeds;
    std::set<std::vector<int>> visited;
    for (const auto& candidate : beam) visited.insert(candidate.nodes);
    generated += visited.size();

    for (int level = 0; level <= budget.maxStations && !beam.empty(); ++level) {
      std::vector<Candidate> next;
      for (const auto& candidate : beam) {
        if (exhausted()) return finish();
        Route trial(original.getId(), vehicle, instance);
        for (size_t i = 1; i + 1 < candidate.nodes.size(); ++i) trial.addNode(candidate.nodes[i]);
        trial.evaluate();
        ++result.evaluations;
        if (trial.isFeasible()) {
          if (!result.route || trial.getTotalDistance() < result.route->getTotalDistance()) result.route = trial;
          continue;
        }
        if (candidate.visits >= budget.maxStations || budget.stationCandidates == 0) continue;

        // Repair the first segment that exceeds even a fully charged battery.
        // Only gaps before its first unreachable node can fix that segment.
        int anchor = 0, firstGap = 1, lastGap = static_cast<int>(candidate.nodes.size()) - 1;
        double used = 0;
        for (int i = 1; i < static_cast<int>(candidate.nodes.size()); ++i) {
          used += instance->getDistance(candidate.nodes[i - 1], candidate.nodes[i]) * rate;
          if (used > battery + 1e-9) {
            firstGap = anchor + 1;
            lastGap = i;
            break;
          }
          if (instance->getNodeType(candidate.nodes[i]) == NodeType::STATION) {
            anchor = i;
            used = 0;
          }
        }
        for (int gap = firstGap; gap <= lastGap; ++gap) {
          if (Clock::now() >= deadline) { result.budgetExhausted = true; return finish(); }
          const int previous = candidate.nodes[gap - 1], following = candidate.nodes[gap];
          double prefixEnergy = 0;
          int lastAnchor = gap - 1;
          while (lastAnchor > 0 && instance->getNodeType(candidate.nodes[lastAnchor]) == NodeType::CUSTOMER) --lastAnchor;
          for (int k = lastAnchor + 1; k < gap; ++k)
            prefixEnergy += instance->getDistance(candidate.nodes[k - 1], candidate.nodes[k]) * rate;
          for (int station : instance->getCandidateStations(previous, following, candidateCount)) {
            if (station == previous || station == following) continue;
            if (prefixEnergy + instance->getDistance(previous, station) * rate > battery + 1e-9) continue;
            auto nodes = candidate.nodes;
            nodes.insert(nodes.begin() + gap, station);
            if (!visited.insert(nodes).second) continue;
            if (++generated > generationLimit) { result.budgetExhausted = true; return finish(); }
            next.push_back(describe(std::move(nodes), *instance, battery, rate));
          }
        }
      }
      // This is a bounded heuristic: prefer feasible plans with fewer added visits,
      // and the best actual distance among plans evaluated at this level.
      if (result.route) return finish();
      const size_t keep = std::min(next.size(), static_cast<size_t>(budget.beamWidth));
      std::partial_sort(next.begin(), next.begin() + keep, next.end(), better);
      next.resize(keep);
      beam = std::move(next);
    }
    if (candidateCount == 0 || candidateCount >= stationCount) return finish();
    // Retry a wider detour list only with remaining time/evaluations. All rounds
    // share the same budget; a narrow failed attempt is not an impossibility proof.
    candidateCount = std::min(stationCount, std::max(candidateCount + 1, candidateCount * 2));
  }
}
} // namespace alns
