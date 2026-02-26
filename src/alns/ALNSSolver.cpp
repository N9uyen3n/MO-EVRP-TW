#include "../../include/alns/ALNSSolver.h"

// --- Logger ---
#include "../../include/logger/ComprehensiveLogger.h"
#include "../../include/logger/NullLogger.h"

// --- Core ---
#include "../../include/alns/ParetoArchive.h"
#include "../../include/alns/ScatterSearch.h"
#include "../../include/core/Customer.h"
#include "../../include/core/Route.h"
#include "../../include/core/Station.h"
#include "../../include/core/Vehicle.h"

// --- Operators: Destroy ---
#include "../../include/alns/operators/destroy/InefficientRouteRemoval.h"
#include "../../include/alns/operators/destroy/RouteMergingDestroy.h"
#include "../../include/alns/operators/destroy/ShawDestroy.h"
#include "../../include/alns/operators/destroy/TargetedStationRemoval.h"
#include "../../include/alns/operators/destroy/UnifiedCostDestroy.h"

// --- Operators: Repair ---
// --- Operators: Repair ---
#include "../../include/alns/operators/repair/AdaptiveInsertion.h"
#include "../../include/alns/operators/repair/GreedyEnergyInsertion.h"
#include "../../include/alns/operators/repair/ParetoFocusRepair.h"
#include "../../include/alns/operators/repair/RegretKRepair.h"
#include "../../include/alns/operators/repair/SmartStationRepair.h"
#include "../../include/alns/operators/repair/SmartTimeAwareStationRepair.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>

namespace alns {
// ******************************************************************
// ** 1. HELPER: OperatorPool Implementation
// ******************************************************************

int OperatorPool::select(std::mt19937 &rng) {
  double totalWeight = std::accumulate(weights.begin(), weights.end(), 0.0);
  if (totalWeight <= 1e-9) {
    std::uniform_int_distribution<> dist(0, operators.size() - 1);
    return dist(rng);
  }
  std::uniform_real_distribution<> dist(0.0, totalWeight);
  double r = dist(rng);
  double currentSum = 0.0;
  for (size_t i = 0; i < operators.size(); ++i) {
    currentSum += weights[i];
    if (r <= currentSum)
      return i;
  }
  return operators.size() - 1;
}

void OperatorPool::updateWeights(double decay) {
  for (size_t i = 0; i < operators.size(); ++i) {
    double avgScore = (usages[i] > 0) ? (scores[i] / usages[i]) : 0.0;
    weights[i] = weights[i] * decay + (1.0 - decay) * avgScore;
    weights[i] = std::max(0.1, weights[i]);
  }
}

void OperatorPool::resetScores() {
  std::fill(scores.begin(), scores.end(), 0.0);
  std::fill(usages.begin(), usages.end(), 0);
}

// ******************************************************************
// ** 2. ALNSSolver Implementation
// ******************************************************************

ALNSSolver::ALNSSolver(std::shared_ptr<Instance> instance, ALNSConfig config,
                       const std::string &outputDirectory,
                       const std::string &runName)
    : instance(instance), s_current(instance), config(config), archive(100),
      localSearch(instance), solutionPool(2, instance), totalCustomers(0),
      runName_(runName),
      scatterSearch_(std::make_unique<ScatterSearch>(
          instance, config.scatterSearchConfig, randomEngine)) {
  unsigned seed =
      (config.randomSeed > 0)
          ? config.randomSeed
          : std::chrono::system_clock::now().time_since_epoch().count();
  this->randomEngine.seed(seed);

  if (config.enableLogging) {
    this->logger = std::make_unique<logging::ComprehensiveLogger>(
        outputDirectory, runName);
    std::cout << "Logging ENABLED. Output: " << outputDirectory << std::endl;
  } else {
    this->logger = std::make_unique<logging::NullLogger>();
  }

  for (const auto &node : instance->getNodes()) {
    if (std::dynamic_pointer_cast<Customer>(node)) {
      this->totalCustomers++;
    }
  }
  this->logger->logConfig(this->config);
  this->currentTemperature = config.startTemperature;

  // ĐĂNG KÝ CÁC TOÁN TỬ
  addDestroyOperator(std::make_shared<UnifiedCostDestroy>(instance, 3),
                     2.0); // High weight due to versatility
  addDestroyOperator(std::make_shared<ShawDestroy>(instance, 6), 2.0);
  addDestroyOperator(std::make_shared<InefficientRouteRemoval>(instance), 2.0);
  addDestroyOperator(std::make_shared<RouteMergingDestroy>(instance), 1.5);
  addDestroyOperator(std::make_shared<TargetedStationRemoval>(instance), 2.0);

  // addRepairOperator(std::make_shared<AdaptiveInsertion>(instance), 2.0);
  addRepairOperator(std::make_shared<RegretKRepair>(instance, config.regretK,
                                                    config.noiseParameter),
                    1.0);
  addRepairOperator(std::make_shared<SmartStationRepair>(instance), 2.0);
  addRepairOperator(std::make_shared<SmartTimeAwareStationRepair>(instance),
                    1.5);
  addRepairOperator(std::make_shared<GreedyEnergyInsertion>(instance), 2.0);
  addRepairOperator(std::make_shared<ParetoFocusRepair>(instance), 1.5);
}

ALNSSolver::~ALNSSolver() = default;

void ALNSSolver::addDestroyOperator(std::shared_ptr<IDestroyOperator> op,
                                    double initialWeight) {
  destroyPool.operators.push_back(op);
  destroyPool.weights.push_back(initialWeight);
  destroyPool.scores.push_back(0.0);
  destroyPool.usages.push_back(0);
}

void ALNSSolver::addRepairOperator(std::shared_ptr<IRepairOperator> op,
                                   double initialWeight) {
  repairPool.operators.push_back(op);
  repairPool.weights.push_back(initialWeight);
  repairPool.scores.push_back(0.0);
  repairPool.usages.push_back(0);
}

int ALNSSolver::calculateNodesToRemove() {
  if (totalCustomers == 0 || config.maxRemoval <= 0.0)
    return 0;
  int min_num = static_cast<int>(totalCustomers * config.minRemoval);
  int max_num = static_cast<int>(totalCustomers * config.maxRemoval *
                                 perturbationBoost_); // Apply boost
  min_num = std::max(1, min_num);
  max_num = std::max(min_num, max_num);
  max_num = std::min(max_num, totalCustomers);
  min_num = std::min(min_num, max_num);
  std::uniform_int_distribution<int> dist(min_num, max_num);
  return dist(randomEngine);
}

// ******************************************************************
// ** 3. MAIN SOLVE LOOP
// ******************************************************************

std::vector<Solution> ALNSSolver::solve() {
  auto startTime = std::chrono::high_resolution_clock::now();

  s_current = generateInitialSolution();
  this->archive.tryAdd(s_current);

  if (!s_current.isFeasible()) {
    std::cerr << "[ERROR] Initial solution is not feasible!" << std::endl;
    return {};
  }

  Solution s_best = s_current;
  currentTemperature = config.startTemperature;
  int iterationsWithoutImprovement = 0;
  int lastMinVeh =
      INT_MAX; // Track global minimum vehicles to prevent premature stop

  std::cout << "[HV] Using 3D Hypervolume (Distance, Gini, MaxTime) with "
               "adaptive normalization, ref z_r = (1.1, 1.1, 1.1)"
            << std::endl;

  // --- PROFILING SETUP ---
  struct TimingStats {
    long long destroy_us = 0;
    long long repair_us = 0;
    long long evaluate_us = 0;
    long long ls_us = 0;
    long long acceptance_us = 0;
  } stats;
  auto now = std::chrono::high_resolution_clock::now;
  decltype(now()) t1, t2, t3, t4, t5, t6;
  // --- END PROFILING SETUP ---

  std::uniform_real_distribution<> dis(0.0, 1.0);

  // MOEA/D-style weight vectors: declared at outer scope so both the SA
  // acceptance block AND the segment-boundary archive jump can access them.
  struct WeightVector {
    double dist, gini, time;
  };
  static const std::vector<WeightVector> weightVectors = {
      {1.00, 0.00, 0.00}, // pure distance
      {0.00, 1.00, 0.00}, // pure gini
      {0.00, 0.00, 1.00}, // pure maxtime
      {0.50, 0.50, 0.00}, // dist + gini
      {0.50, 0.00, 0.50}, // dist + maxtime
      {0.00, 0.50, 0.50}, // gini + maxtime
      {0.60, 0.25, 0.15}, // dist-heavy
      {0.60, 0.15, 0.25}, // dist-heavy (flipped)
      {0.20, 0.55, 0.25}, // gini-heavy
      {0.20, 0.25, 0.55}, // maxtime-heavy
      {0.33, 0.33, 0.34}, // balanced
  };

  for (int i = 0; i < config.maxIterations; ++i) {
    Solution &s_new = solutionPool.acquire();
    s_new = this->s_current;

    t1 = now(); // Start Destroy

    int n_to_remove = calculateNodesToRemove();
    int destroy_op_idx = destroyPool.select(randomEngine);
    auto destroy_op = std::static_pointer_cast<IDestroyOperator>(
        destroyPool.operators[destroy_op_idx]);
    std::vector<int> unserved_custs =
        destroy_op->execute(s_new, n_to_remove, randomEngine);
    destroyPool.usages[destroy_op_idx]++;

    t2 = now(); // Start Repair
    stats.destroy_us +=
        std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();

    int repair_op_idx = repairPool.select(randomEngine);
    auto repair_op = std::static_pointer_cast<IRepairOperator>(
        repairPool.operators[repair_op_idx]);
    repair_op->execute(s_new, unserved_custs, randomEngine);
    repairPool.usages[repair_op_idx]++;

    t3 = now(); // Start Evaluate
    stats.repair_us +=
        std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count();

    s_new.evaluateRoutes();

    t4 = now(); // Start Local Search
    stats.evaluate_us +=
        std::chrono::duration_cast<std::chrono::microseconds>(t4 - t3).count();

    if (!s_new.isFeasible()) {
      destroyPool.scores[destroy_op_idx] += config.scoreIdentical;
      repairPool.scores[repair_op_idx] += config.scoreIdentical;
      solutionPool.release(s_new);
      continue;
    }

    if (config.useLocalSearch) {
      std::uniform_int_distribution<> dis_ls(0, 99);
      if (dis_ls(randomEngine) < config.localSearchIntensity) {
        localSearch.run(s_new);
      }
    }

    t5 = now(); // Start Acceptance
    stats.ls_us +=
        std::chrono::duration_cast<std::chrono::microseconds>(t5 - t4).count();

    std::string result = "Rejected";
    bool improved = false; // Track whether this iteration improved

    if (s_new.dominates(s_current)) {
      // Dominating: update current, score, add to archive
      archive.tryAdd(s_new);
      s_current = s_new;
      destroyPool.scores[destroy_op_idx] += config.scoreDominating;
      repairPool.scores[repair_op_idx] += config.scoreDominating;
      improved = true;
      result = "Dominating";
    } else {
      // Non-dominating: try archive first, then apply SA
      AddResult add_res = archive.tryAdd(s_new);
      if (add_res == AddResult::DOMINATING ||
          add_res == AddResult::NON_DOMINATED) {
        destroyPool.scores[destroy_op_idx] += config.scoreNonDominated;
        repairPool.scores[repair_op_idx] += config.scoreNonDominated;
        improved = true;
        result = "Non-Dominated";
        // Do NOT force s_current = s_new. Let SA decide independently.
      }

      // weightVectors defined at outer scope (before for loop).
      // Cycle through them based on the current segment.
      int wIdx =
          (i / std::max(1, config.segmentIterations)) % weightVectors.size();
      const auto &w = weightVectors[wIdx];

      // Dynamic scaling to align dimensions.
      // Cap scales to prevent explosion when an objective is near zero.
      double baseDist = std::max(1.0, s_current.getTotalDistance());
      double giniScale = std::min(
          500.0, baseDist / std::max(0.01, s_current.getWorkloadGini()));
      double timeScale =
          std::min(10.0, baseDist / std::max(1.0, s_current.getMaxTime()));

      // veh_penalty: strong but not absolute — 3x baseDist lets SA occasionally
      // accept one extra vehicle during exploration (instead of 1000 hard
      // floor).
      double veh_penalty = std::max(500.0, baseDist * 3.0);

      double delta_objectives =
          veh_penalty *
              (s_new.getTotalVehicles() - s_current.getTotalVehicles()) +
          w.dist * (s_new.getTotalDistance() - s_current.getTotalDistance()) +
          w.gini * giniScale *
              (s_new.getWorkloadGini() - s_current.getWorkloadGini()) +
          w.time * timeScale * (s_new.getMaxTime() - s_current.getMaxTime());

      if (std::exp(-delta_objectives / currentTemperature) >
          dis(randomEngine)) {
        s_current = s_new;
        if (result == "Rejected") {
          destroyPool.scores[destroy_op_idx] += config.scoreDominated;
          repairPool.scores[repair_op_idx] += config.scoreDominated;
          result = "Accepted (SA)";
        } else {
          result += " & Accepted (SA)";
        }
      } else {
        if (result == "Rejected") {
          destroyPool.scores[destroy_op_idx] += config.scoreIdentical;
          repairPool.scores[repair_op_idx] += config.scoreIdentical;
        }
      }
    }

    // [FIX Bug 1] Only increment stagnation counter when no improvement
    if (improved) {
      iterationsWithoutImprovement = 0;
    } else {
      iterationsWithoutImprovement++;
    }

    t6 = now();
    stats.acceptance_us +=
        std::chrono::duration_cast<std::chrono::microseconds>(t6 - t5).count();

    logger->logEvolutionStep(i, destroy_op->getName(), repair_op->getName(),
                             result, s_new);

    currentTemperature *= config.coolingRate;
    if (currentTemperature < config.minTemperature) {
      currentTemperature = config.minTemperature;
    }

    // ⭐ PERTURBATION MECHANISM: Escape local optima when stagnated
    // Phase 1: Mild perturbation (reheating only) at 500 iterations
    if (iterationsWithoutImprovement > 0 &&
        iterationsWithoutImprovement % 500 == 0 &&
        iterationsWithoutImprovement < 2000) {
      currentTemperature =
          std::min(currentTemperature * 1.5, config.startTemperature * 0.7);
    }

    // Phase 2: Strong perturbation at 1000+ iterations
    if (iterationsWithoutImprovement > 0 &&
        iterationsWithoutImprovement % 1000 == 0) {
      // Aggressive reheating
      currentTemperature = config.startTemperature * 0.8;

      // Jump to a random solution from Pareto archive for diversification
      auto &front = archive.getFront();
      if (front.size() > 1) {
        std::uniform_int_distribution<> archiveDist(0, front.size() - 1);
        int idx = archiveDist(randomEngine);
        s_current = front[idx];
        std::cout << "[Perturbation] Iter " << i
                  << ": Jumping to archive solution #" << idx << " (stagnated "
                  << iterationsWithoutImprovement << " iters)" << std::endl;
      }
    }

    // Phase 3: Very strong perturbation at 2000+ iterations - increase destroy
    // intensity
    if (iterationsWithoutImprovement > 4000) {
      perturbationBoost_ = 1.5; // Destroy 50% more nodes
    } else if (iterationsWithoutImprovement > 2000) {
      perturbationBoost_ = 1.25; // Destroy 25% more nodes
    } else {
      perturbationBoost_ = 1.0;
    }

    if ((i + 1) % config.segmentIterations == 0) {
      // Segment boundary: jump s_current to the archive solution that best fits
      // the NEXT weight vector, ensuring SA starts each segment coherently.
      int nextWIdx = ((i + 1) / std::max(1, config.segmentIterations)) %
                     weightVectors.size();
      const auto &nextW = weightVectors[nextWIdx];

      auto &front = archive.getFront();
      if (front.size() > 1) {
        double minDist = front[0].getTotalDistance(), maxDist = minDist;
        double minGini = front[0].getWorkloadGini(), maxGini = minGini;
        double minTime = front[0].getMaxTime(), maxTime = minTime;
        for (const auto &sol : front) {
          minDist = std::min(minDist, sol.getTotalDistance());
          maxDist = std::max(maxDist, sol.getTotalDistance());
          minGini = std::min(minGini, sol.getWorkloadGini());
          maxGini = std::max(maxGini, sol.getWorkloadGini());
          minTime = std::min(minTime, sol.getMaxTime());
          maxTime = std::max(maxTime, sol.getMaxTime());
        }
        double distR = std::max(1e-6, maxDist - minDist);
        double giniR = std::max(1e-6, maxGini - minGini);
        double timeR = std::max(1e-6, maxTime - minTime);

        double bestScalar = std::numeric_limits<double>::max();
        const Solution *bestSol = nullptr;
        for (const auto &sol : front) {
          double scalar =
              nextW.dist * (sol.getTotalDistance() - minDist) / distR +
              nextW.gini * (sol.getWorkloadGini() - minGini) / giniR +
              nextW.time * (sol.getMaxTime() - minTime) / timeR;
          if (scalar < bestScalar) {
            bestScalar = scalar;
            bestSol = &sol;
          }
        }
        if (bestSol)
          s_current = *bestSol;
      }
      auto now_progress = std::chrono::high_resolution_clock::now();
      long long time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              now_progress - startTime)
                              .count();
      logger->logProgress(i + 1, time_ms, this->archive);
      logger->logOperatorSegment(i + 1, destroyPool, repairPool);
      destroyPool.updateWeights(config.decayParameter);
      repairPool.updateWeights(config.decayParameter);
      destroyPool.resetScores();
      repairPool.resetScores();

      // ⭐ HV-BASED CONVERGENCE CHECK
      if (archive.getSize() > 0) {
        // Track min vehicles to handle hierarchical dominance resets
        int currentMinVeh = INT_MAX;
        double archiveBestDist = std::numeric_limits<double>::max();
        for (const auto &sol : archive.getFront()) {
          currentMinVeh = std::min(currentMinVeh, sol.getTotalVehicles());
          archiveBestDist = std::min(archiveBestDist, sol.getTotalDistance());
        }

        if (currentMinVeh < lastMinVeh) {
          // Breakthrough — found a solution with fewer vehicles!
          // Due to hierarchical dominance, the archive heavily shrinks, so HV
          // drops. Therefore, we must reset the stopping criterion completely.
          std::cout << "[VEH-Improve] New min vehicles: " << currentMinVeh
                    << " (was " << lastMinVeh << "). Resetting HV tracking."
                    << std::endl;
          lastMinVeh = currentMinVeh;
          hvStagnationCount_ = 0;
          previousHV_ = 0.0; // Reset watermark
        } else {
          // Same vehicle level → check HV stagnation normally
          double currentHV = archive.computeHypervolume();
          double hvImprovement = 0.0;

          if (previousHV_ == 0.0) {
            // First calculation after a vehicle drop reset
            hvImprovement = 1.0;
            previousHV_ = currentHV;
            hvStagnationCount_ = 0;
          } else {
            hvImprovement = (currentHV - previousHV_) / previousHV_;

            if (currentHV >
                previousHV_ * (1.0 + config.hvImprovementThreshold)) {
              // Actual improvement beyond threshold → reset stagnation, update
              // watermark
              hvStagnationCount_ = 0;
              previousHV_ = currentHV;
            } else {
              // Plateau or decrease → counts as stagnation
              hvStagnationCount_++;
            }
          }

          std::cout << "[HV] Segment " << (i + 1) << ": HV=" << std::fixed
                    << std::setprecision(2) << currentHV
                    << " (best=" << std::setprecision(2) << previousHV_ << ")"
                    << ", Delta=" << std::setprecision(4)
                    << (hvImprovement * 100.0)
                    << "%, Stagnation=" << hvStagnationCount_ << "/"
                    << config.hvStagnationLimit
                    << " | Archive: size=" << archive.getSize()
                    << ", minVeh=" << currentMinVeh
                    << ", bestDist=" << std::setprecision(1) << archiveBestDist
                    << std::endl;

          if (hvStagnationCount_ >= config.hvStagnationLimit) {
            std::cout << "[HV-Stop] Converged: Hypervolume stable for "
                      << hvStagnationCount_ << " consecutive segments ("
                      << (i + 1) << " iterations)." << std::endl;
            break;
          }
        }
      }
    }

    // [REMOVED] Counter now managed above after acceptance logic
    solutionPool.release(s_new);
  }

  auto endTime = std::chrono::high_resolution_clock::now();
  long long total_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime)
          .count();

  // --- PRINT PROFILING RESULTS ---
  long long total_us = stats.destroy_us + stats.repair_us + stats.evaluate_us +
                       stats.ls_us + stats.acceptance_us;
  if (total_us == 0)
    total_us = 1; // Avoid division by zero
  std::cout << "\n\n=== ALNS Iteration Breakdown (" << config.maxIterations
            << " iters) ===\n";
  std::cout << std::fixed << std::setprecision(2);
  std::cout << "Component          Time (ms)    Percentage\n";
  std::cout << "-------------------------------------------\n";
  std::cout << "Destroy            " << std::setw(10)
            << stats.destroy_us / 1000.0 << "    " << std::setw(8)
            << (100.0 * stats.destroy_us / total_us) << "%\n";
  std::cout << "Repair             " << std::setw(10)
            << stats.repair_us / 1000.0 << "    " << std::setw(8)
            << (100.0 * stats.repair_us / total_us) << "%\n";
  std::cout << "Evaluate           " << std::setw(10)
            << stats.evaluate_us / 1000.0 << "    " << std::setw(8)
            << (100.0 * stats.evaluate_us / total_us) << "%\n";
  std::cout << "LocalSearch        " << std::setw(10) << stats.ls_us / 1000.0
            << "    " << std::setw(8) << (100.0 * stats.ls_us / total_us)
            << "%\n";
  std::cout << "Acceptance         " << std::setw(10)
            << stats.acceptance_us / 1000.0 << "    " << std::setw(8)
            << (100.0 * stats.acceptance_us / total_us) << "%\n";
  std::cout << "-------------------------------------------\n";
  std::cout << "Total Profiled:    " << std::setw(10) << total_us / 1000.0
            << "    " << "100.00%\n";
  std::cout << "===========================================\n\n";
  // --- END PROFILING ---

  // ⭐ SCATTER SEARCH PHASE (Post-ALNS Intensification)
  if (config.useScatterSearch && archive.getSize() >= 2) {
    std::cout << "\n[SS] Starting Scatter Search intensification phase..."
              << std::endl;
    // Re-seed scatterSearch_ with current rng state
    int newSols = scatterSearch_->run(archive, *this);
    std::cout << "[SS] Scatter Search done. Added " << newSols
              << " new solution(s) to Pareto Archive." << std::endl;
    // Reset operator weights so they reflect only post-SS state
    destroyPool.resetScores();
    repairPool.resetScores();
  }

  logger->logFinalFront(this->archive);
  logger->logSummary(total_ms, config.maxIterations, this->archive.getSize());

  std::cout << "ALNS Finished." << std::endl;
  return archive.getFront();
}

// ******************************************************************
// ** 4. IMPROVE SOLUTION (ALNS Mini-Loop for ScatterSearch)
// ******************************************************************

void ALNSSolver::improveSolution(Solution &sol, int maxIters) {
  // Use a temporary current solution starting from the given solution
  Solution s_imp = sol;
  double temp = config.startTemperature * 0.5; // Start at half temperature
  const double cooling = config.coolingRate;
  const double minTemp = config.minTemperature;
  std::uniform_real_distribution<> dis(0.0, 1.0);

  for (int i = 0; i < maxIters; ++i) {
    Solution &s_new = solutionPool.acquire();
    s_new = s_imp;

    // Destroy
    int n_remove = calculateNodesToRemove();
    int d_idx = destroyPool.select(randomEngine);
    auto d_op = std::static_pointer_cast<IDestroyOperator>(
        destroyPool.operators[d_idx]);
    std::vector<int> unserved = d_op->execute(s_new, n_remove, randomEngine);

    // Repair
    int r_idx = repairPool.select(randomEngine);
    auto r_op =
        std::static_pointer_cast<IRepairOperator>(repairPool.operators[r_idx]);
    r_op->execute(s_new, unserved, randomEngine);

    s_new.evaluateRoutes();

    if (!s_new.isFeasible()) {
      solutionPool.release(s_new);
      continue;
    }

    // Optional LS
    if (config.useLocalSearch) {
      std::uniform_int_distribution<> dis_ls(0, 99);
      if (dis_ls(randomEngine) < config.localSearchIntensity) {
        localSearch.run(s_new);
      }
    }

    // Acceptance (dominance or SA)
    if (s_new.dominates(s_imp)) {
      s_imp = s_new;
    } else {
      double veh_penalty = 10000;
      double delta =
          veh_penalty * (s_new.getTotalVehicles() - s_imp.getTotalVehicles()) +
          (s_new.getTotalDistance() - s_imp.getTotalDistance());
      if (std::exp(-delta / temp) > dis(randomEngine)) {
        s_imp = s_new;
      }
    }

    temp = std::max(temp * cooling, minTemp);
    solutionPool.release(s_new);
  }

  // Return the best found: use s_imp if better, else keep original sol
  if (s_imp.isFeasible() && s_imp.dominates(sol)) {
    sol = s_imp;
  } else if (s_imp.isFeasible() &&
             s_imp.getTotalVehicles() == sol.getTotalVehicles() &&
             s_imp.getTotalDistance() < sol.getTotalDistance() - 1e-4) {
    sol = s_imp;
  }
}

// ******************************************************************
// ** 5. INITIAL SOLUTION (Simple Greedy)
// ******************************************************************
// File: src/alns/ALNSSolver.cpp

Solution ALNSSolver::generateInitialSolution() {
  std::cout
      << "[Info] Generating Initial Solution (Corrected for Instance API)..."
      << std::endl;

  if (!instance->getNodes().empty()) {
    std::cout << "Debug: Node count = " << instance->getNodes().size()
              << std::endl;
  } else {
    std::cout << "Debug: Nodes are empty!" << std::endl;
    return Solution(instance);
  }

  Solution sol(instance);

  // --- BƯỚC 1: TẠO LOOKUP TABLE ---
  int maxId = 0;
  for (const auto &node : instance->getNodes()) {
    if (node->getId() > maxId)
      maxId = node->getId();
  }
  std::cout << "Debug: Max ID found = " << maxId << std::endl;

  // Validate Max ID vs Vector Size implies contiguous IDs? Not necessarily, but
  // safe for lookup.
  if (maxId > 100000) {
    std::cerr << "[Warning] MaxID is suspiciously large: " << maxId
              << ". Possible ID corruption?" << std::endl;
  }

  std::vector<Customer *> custLookup(maxId + 1, nullptr);
  std::vector<Station *> stationLookup(maxId + 1, nullptr);
  std::vector<int> unservedIds;
  std::vector<int> stationIds;

  // Safe population of lookups
  for (const auto &c : instance->getCustomers()) {
    int cid = c->getId();
    if (cid < 0 || cid > maxId) {
      std::cerr << "[Error] Customer ID " << cid
                << " out of bounds (Max: " << maxId << ")" << std::endl;
      continue;
    }
    custLookup[cid] = c.get();
    unservedIds.push_back(cid);
  }

  for (const auto &s : instance->getStations()) {
    int sid = s->getId();
    if (sid < 0 || sid > maxId) {
      std::cerr << "[Error] Station ID " << sid
                << " out of bounds (Max: " << maxId << ")" << std::endl;
      continue;
    }
    stationLookup[sid] = s.get();
    stationIds.push_back(sid);
  }

  // Lấy Depot - Safe verify
  auto depotNode = instance->getNodeById(0); // Assuming 0 is depot
  if (!depotNode) {
    std::cerr << "[FATAL] Depot not found at ID 0!" << std::endl;
    return sol;
  }
  const double maxTime = depotNode->getDueDate();

  const double vehCapacity = instance->getVehicleCapacity();
  const double vehMaxBattery = instance->getVehicleBattery();
  const double vehEnergyRate = instance->getVehicleEnergyRate();
  const int depotId = 0;

  std::cout << "Total customers to serve: " << unservedIds.size() << "\n";
  std::cout << "Stations available: " << stationIds.size() << "\n";
  std::cout << "Vehicle params: Cap=" << vehCapacity
            << ", Batt=" << vehMaxBattery << "\n";

  auto depotX = depotNode->getX();
  auto depotNodeY = depotNode->getY();

  // ========================================================================
  // FIX 1: Smart Sorting - Detect instance type and use appropriate strategy
  // ========================================================================

  // Detect instance type from runName (e.g., "r107_21", "c101_21", "rc108C15")
  bool isRInstance = false;
  {
    std::string lowerName = runName_;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                   ::tolower);
    // R instances start with 'r' (covers both 'r' and 'rc')
    isRInstance = (lowerName[0] == 'r');
  }

  if (isRInstance) {
    // R/RC instances: Sort by Time Window Width (tightest deadlines first)
    std::cout << "[Info] R-instance detected. Sorting by Time Window Width..."
              << std::endl;
    std::sort(unservedIds.begin(), unservedIds.end(), [&](int a, int b) {
      double twA = custLookup[a]->getDueDate() - custLookup[a]->getReadyTime();
      double twB = custLookup[b]->getDueDate() - custLookup[b]->getReadyTime();
      return twA < twB; // Narrow time windows first
    });
    std::cout << "Sorted " << unservedIds.size() << " customers by TW width.\n";
  } else {
    // C instances: Use Clarke-Wright Savings
    std::cout << "[Info] C-instance detected. Applying Clarke-Wright Savings..."
              << std::endl;

    struct Saving {
      int custI;
      int custJ;
      double value;

      bool operator<(const Saving &other) const {
        return value > other.value; // Descending order
      }
    };

    std::vector<Saving> savings;

    // Calculate savings for all customer pairs
    for (size_t i = 0; i < unservedIds.size(); ++i) {
      for (size_t j = i + 1; j < unservedIds.size(); ++j) {
        int custI = unservedIds[i];
        int custJ = unservedIds[j];

        double saving = instance->getDistance(depotId, custI) +
                        instance->getDistance(depotId, custJ) -
                        instance->getDistance(custI, custJ);

        if (saving > 0.0) {
          savings.push_back({custI, custJ, saving});
        }
      }
    }

    std::sort(savings.begin(), savings.end());

    std::vector<int> sortedIds;
    std::vector<bool> added(maxId + 1, false);

    for (const auto &s : savings) {
      if (!added[s.custI] && !added[s.custJ]) {
        sortedIds.push_back(s.custI);
        sortedIds.push_back(s.custJ);
        added[s.custI] = true;
        added[s.custJ] = true;
      } else if (!added[s.custI]) {
        sortedIds.push_back(s.custI);
        added[s.custI] = true;
      } else if (!added[s.custJ]) {
        sortedIds.push_back(s.custJ);
        added[s.custJ] = true;
      }
    }

    for (int custId : unservedIds) {
      if (!added[custId]) {
        sortedIds.push_back(custId);
      }
    }

    unservedIds = sortedIds;
    std::cout << "Sorted " << unservedIds.size()
              << " customers by CW Savings.\n";
  }
  // ========================================================================

  bool cannotServeMore = false;
  int consecutiveFailures = 0; // Retry limit to prevent infinite rebuild loops
  const int maxConsecutiveFailures = 3;

  // --- BƯỚC 2: VÒNG LẶP CHÍNH (Constructive Heuristic) ---
  while (!unservedIds.empty() && !cannotServeMore) {
    // Tạo xe mới
    auto vehicle = std::make_shared<Vehicle>(sol.getNumRoutes(), vehCapacity,
                                             vehMaxBattery, vehEnergyRate);
    Route currentRoute(sol.getNumRoutes(), vehicle, instance);

    int currNodeId = depotId;
    double currTime = 0.0;
    double currBatt = vehMaxBattery;
    double currLoad = 0.0;

    bool routeFinished = false;
    bool customerAddedInThisRoute = false;
    std::vector<int>
        servedInThisRoute; // FIX 2: Track customers added to this route

    while (!routeFinished) {
      int bestCustId = -1;
      double bestScore = std::numeric_limits<double>::max();

      // --- TÌM KHÁCH HÀNG TỐT NHẤT ---
      for (int custId : unservedIds) {
        // Lấy pointer từ lookup table (O(1)) -> KHÔNG dùng
        // instance->getDemand(id) vì không có hàm đó
        Customer *cust = custLookup[custId];

        // Check Tải trọng
        if (currLoad + cust->getDemand() > vehCapacity)
          continue;

        // Check Pin & Thời gian
        double dist = instance->getDistance(currNodeId, custId);
        double energyNeeded = dist * vehEnergyRate;
        if (currBatt < energyNeeded)
          continue;

        double travelTime = instance->getTime(currNodeId, custId);
        double arrivalTime = currTime + travelTime;

        // Check Time Window (ReadyTime & DueDate nằm trong class Customer)
        if (arrivalTime > cust->getDueDate())
          continue;

        // --- LOGIC LOOK-AHEAD: Phải đảm bảo về được nhà hoặc tới trạm ---
        double startService = std::max(arrivalTime, cust->getReadyTime());
        double endService = startService + cust->getServiceTime();
        double energyLeftAtCust = currBatt - energyNeeded;

        // Check về Depot
        double distToDepot = instance->getDistance(custId, depotId);
        double energyToDepot = distToDepot * vehEnergyRate;
        double timeToDepot = instance->getTime(custId, depotId);

        bool safeReturn = false;

        if (energyLeftAtCust >= energyToDepot &&
            (endService + timeToDepot <= maxTime)) {
          safeReturn = true;
        } else {
          // Check trạm sạc gần nhất (Dùng instance->getNearestStationId)
          int nearStationId = instance->getNearestStationId(custId);
          if (nearStationId != -1) {
            double distToStat = instance->getDistance(custId, nearStationId);
            double energyToStat = distToStat * vehEnergyRate;

            if (energyLeftAtCust >= energyToStat) {
              // Tính sơ bộ thời gian
              double timeToStat = instance->getTime(custId, nearStationId);
              Station *stat = stationLookup[nearStationId];

              // Giả sử sạc đầy để check an toàn
              double arrivalStat = endService + timeToStat;
              double chargeNeeded =
                  vehMaxBattery - (energyLeftAtCust - energyToStat);
              double chargeTime =
                  chargeNeeded * stat->getChargingRate(); // Lấy rate từ lookup

              double timeStatToDepot =
                  instance->getTime(nearStationId, depotId);
              if (arrivalStat + chargeTime + timeStatToDepot <= maxTime) {
                safeReturn = true;
              }
            }
          }
        }

        if (!safeReturn)
          continue;

        // --- FIX 3: NORMALIZED SCORING ---
        // Normalize urgency and distance to [0,1] range
        double slack = cust->getDueDate() - currTime;
        double urgencyNorm = 1.0 - (slack / maxTime); // Near deadline = 1.0
        urgencyNorm = std::max(0.0, std::min(1.0, urgencyNorm));

        double maxDist = instance->getMaxDistance();
        double distNorm = dist / std::max(1.0, maxDist);

        // Combined score: 70% distance, 30% urgency
        double score = 0.7 * distNorm - 0.3 * urgencyNorm;

        if (score < bestScore) {
          bestScore = score;
          bestCustId = custId;
        }
      }

      // --- XỬ LÝ KẾT QUẢ ---
      if (bestCustId != -1) {
        currentRoute.addNode(bestCustId, currentRoute.getNodes().size() - 1);
        Customer *cust = custLookup[bestCustId];

        // Cập nhật trạng thái xe tạm thời
        double dist = instance->getDistance(currNodeId, bestCustId);
        double travelTime = instance->getTime(currNodeId, bestCustId);
        double arrivalTime = currTime + travelTime;
        double waitTime = std::max(0.0, cust->getReadyTime() - arrivalTime);

        currTime = arrivalTime + waitTime + cust->getServiceTime();
        currBatt -= (dist * vehEnergyRate);
        currLoad += cust->getDemand();
        currNodeId = bestCustId;

        // FIX 2: Track and remove from unserved
        servedInThisRoute.push_back(bestCustId); // Track for rollback
        for (size_t i = 0; i < unservedIds.size(); ++i) {
          if (unservedIds[i] == bestCustId) {
            unservedIds[i] = unservedIds.back();
            unservedIds.pop_back();
            break;
          }
        }
        customerAddedInThisRoute = true;
      } else {
        // Không đón được ai -> Thử đi sạc
        // FIX 4: Relaxed charging condition
        bool shouldCharge = (currBatt < vehMaxBattery * 0.8);
        if (!shouldCharge) {
          // Check if any customer is unreachable NOW but reachable with full
          // battery
          for (int cId : unservedIds) {
            double d = instance->getDistance(currNodeId, cId);
            double energyNeeded = d * vehEnergyRate;
            // Only charge if: can't reach now BUT could reach with full battery
            if (currBatt < energyNeeded && energyNeeded <= vehMaxBattery) {
              shouldCharge = true;
              break;
            }
          }
        }

        if (shouldCharge) {
          int bestStationId = -1;
          double minStationDist = std::numeric_limits<double>::max();

          for (int sid : stationIds) {
            double d = instance->getDistance(currNodeId, sid);
            double e = d * vehEnergyRate;

            if (currBatt >= e) {
              // Check thời gian về depot sau sạc
              double tToStat = instance->getTime(currNodeId, sid);
              Station *stat = stationLookup[sid];

              // Tính sạc đầy
              double chargeAmount = vehMaxBattery - (currBatt - e);
              double tCharge = chargeAmount * stat->getChargingRate();
              double tToDepot = instance->getTime(sid, depotId);

              if (currTime + tToStat + tCharge + tToDepot <= maxTime) {
                if (d < minStationDist) {
                  minStationDist = d;
                  bestStationId = sid;
                }
              }
            }
          }

          if (bestStationId != -1) {
            // Đi sạc
            currentRoute.addNode(bestStationId,
                                 currentRoute.getNodes().size() - 1);
            Station *stat = stationLookup[bestStationId];

            double d = instance->getDistance(currNodeId, bestStationId);
            double e = d * vehEnergyRate;
            double t = instance->getTime(currNodeId, bestStationId);

            double chargeAmount = vehMaxBattery - (currBatt - e);
            double chargeTime = chargeAmount * stat->getChargingRate();

            currBatt = vehMaxBattery;
            currTime += t + chargeTime;
            currNodeId = bestStationId;

            continue; // Sạc xong, quay lại tìm khách tiếp
          }
        }

        // Không khách, không trạm -> Kết thúc route
        routeFinished = true;
      }
    }

    if (customerAddedInThisRoute) {
      // FIX 2: Recover lost customers when route is infeasible
      currentRoute.evaluate();
      if (currentRoute.isFeasible()) {
        sol.addRoute(currentRoute);
        consecutiveFailures = 0; // Reset on success
      } else {
        // Route is infeasible - return customers to unserved list
        for (int custId : servedInThisRoute) {
          unservedIds.push_back(custId);
        }
        consecutiveFailures++;
        if (servedInThisRoute.size() <= 1 ||
            consecutiveFailures >= maxConsecutiveFailures) {
          cannotServeMore = true;
        }
      }
    } else {
      cannotServeMore = true;
    }
  }

  // --- FALLBACK: Force-create individual routes for remaining unserved
  // customers ---
  if (!unservedIds.empty()) {
    std::cout << "[Info] Fallback: Creating individual routes for "
              << unservedIds.size() << " remaining customers...\n";
    std::vector<int> stillUnserved;
    for (int custId : unservedIds) {
      auto vehicle = std::make_shared<Vehicle>(sol.getNumRoutes(), vehCapacity,
                                               vehMaxBattery, vehEnergyRate);
      Route singleRoute(sol.getNumRoutes(), vehicle, instance);
      singleRoute.addNode(custId, singleRoute.getNodes().size() - 1);
      singleRoute.evaluate();

      if (singleRoute.isFeasible()) {
        sol.addRoute(singleRoute);
      } else {
        // Try inserting ANY station EITHER after OR before customer
        bool repaired = false;

        // Iterate through ALL stations to find a feasible one
        for (int stationId : stationIds) {
          // Check Depot -> Cust -> Station -> Depot
          Route routeWithStationAfter = singleRoute; // Copy
          // Current nodes: [Depot, Cust, Depot] (size 3)
          // Insert at index 2 (before end depot)
          routeWithStationAfter.addNode(stationId, 2);
          routeWithStationAfter.evaluate();

          if (routeWithStationAfter.isFeasible()) {
            sol.addRoute(routeWithStationAfter);
            repaired = true;
            break;
          }

          // Check Depot -> Station -> Cust -> Depot
          Route routeWithStationBefore = singleRoute; // Copy original single
          // Insert at index 1 (before cust)
          routeWithStationBefore.addNode(stationId, 1);
          routeWithStationBefore.evaluate();

          if (routeWithStationBefore.isFeasible()) {
            sol.addRoute(routeWithStationBefore);
            repaired = true;
            break;
          }
        }

        if (!repaired) {
          // Level 2 Fallback: Try Double Station Insertion (D -> Si -> C -> Sj
          // -> D) std::cout << "[Info] Cust " << custId << " needs double
          // station insertion. Searching 2-station paths...\n";
          for (int s1 : stationIds) {
            for (int s2 : stationIds) {
              Route routeDouble = singleRoute; // Base: [D, C, D]
              // Insert s1 before C (index 1) -> [D, s1, C, D]
              routeDouble.addNode(s1, 1);
              // Insert s2 after C (index 3) -> [D, s1, C, s2, D]
              routeDouble.addNode(s2, 3);

              routeDouble.evaluate();
              if (routeDouble.isFeasible()) {
                sol.addRoute(routeDouble);
                repaired = true;
                // std::cout << "[Fixed] Covered Cust " << custId << " with
                // stations " << s1 << " & " << s2 << "\n";
                goto double_break; // Break both loops
              }
            }
          }
        double_break:;
        }

        if (!repaired) {
          stillUnserved.push_back(custId);
          std::cerr
              << "[Warning] Customer " << custId
              << " cannot be served even with DOUBLE station insertion!\n";
        }
      }
    }
    unservedIds = stillUnserved;
  }

  // Check nếu còn sót khách
  if (!unservedIds.empty()) {
    std::cerr << "[Warning] Could not serve " << unservedIds.size()
              << " customers.\n";
  }

  sol.evaluateRoutes(); // [FIX] Calculate totals before returning

  std::cout << "\n========================================\n";
  std::cout << "Initial Solution Summary:\n";
  std::cout << "  Routes: " << sol.getNumRoutes() << "\n";
  std::cout << "  Unserved: " << unservedIds.size() << "\n";
  std::cout << "  Total Distance: " << sol.getTotalDistance() << "\n";
  std::cout << "  Feasible: " << (sol.isFeasible() ? "YES" : "NO") << "\n";
  std::cout << "========================================\n";

  return sol;
}
} // namespace alns
