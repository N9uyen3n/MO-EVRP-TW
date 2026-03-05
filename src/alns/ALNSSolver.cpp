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
#include "../../include/alns/operators/destroy/RandomRemoval.h"
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

  // ĐĂNG KÝ CÁC TOÁN TỬ TRỌNG TÂM DISTANCE
  addDestroyOperator(std::make_shared<UnifiedCostDestroy>(instance, 3),
                     2.0); // Cost-based (distance/time)
  addDestroyOperator(std::make_shared<ShawDestroy>(instance, 6),
                     2.0); // Tăng cường Shaw phá cụm
  addDestroyOperator(std::make_shared<InefficientRouteRemoval>(instance), 1.0);

  addDestroyOperator(std::make_shared<RouteMergingDestroy>(instance),
                     2.0); // Focus distance (giảm mớ merge xe)
  addDestroyOperator(std::make_shared<TargetedStationRemoval>(instance),
                     1.0); // Giảm bớt station removal
  // addDestroyOperator(std::make_shared<RandomRemoval>(instance), 1.0);

  // SỬA CÁC TOÁN TỬ REPAIR THIÊN VỀ DISTANCE
  addRepairOperator(std::make_shared<AdaptiveInsertion>(instance), 2.0);
  // addRepairOperator(std::make_shared<RegretKRepair>(instance, config.regretK,
  //                                                   config.noiseParameter),
  //                   2.0);

  addRepairOperator(std::make_shared<GreedyEnergyInsertion>(instance), 1.5);
  addRepairOperator(std::make_shared<SmartStationRepair>(instance), 2.0);
  addRepairOperator(std::make_shared<SmartTimeAwareStationRepair>(instance), 1.5);
  addRepairOperator(std::make_shared<GreedyEnergyInsertion>(instance), 0.7);

  // // Tăng cường Pareto (Đa mục tiêu, bao gồm distance)
  // addRepairOperator(std::make_shared<ParetoFocusRepair>(instance), 1.5);
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
  int totalStagnationEver_ = 0; // Đếm số iter không cải tiến liên tục, chỉ
  // reset khi thật sự có improved
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
      {0.00, 0.60, 0.40}, // gini + maxtime
      {0.60, 0.25, 0.15}, // dist-heavy
      {0.10, 0.65, 0.25}, // gini-heavy
      {0.20, 0.30, 0.50}, // maxtime-heavy
      {0.33, 0.34, 0.33}, // balanced
  };

  for (int i = 0; i < config.maxIterations; ++i) {
    Solution &s_new = solutionPool.acquire();
    s_new = this->s_current;

    // Compute current weight vector early (needed for repair hint + SA)
    int wIdx =
        (i / std::max(1, config.segmentIterations)) % weightVectors.size();
    const auto &w = weightVectors[wIdx];

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
    // Wire weight hint if operator is AdaptiveInsertion (Fix 2)
    if (auto adaptive =
            std::dynamic_pointer_cast<AdaptiveInsertion>(repair_op)) {
      adaptive->setWeightHint(w.dist, w.gini, w.time);
    }
    repair_op->execute(s_new, unserved_custs, randomEngine);
    repairPool.usages[repair_op_idx]++;

    t3 = now(); // Start Evaluate
    stats.repair_us +=
        std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count();

    s_new.evaluateRoutes();

    t4 = now(); // Start Local Search
    stats.evaluate_us +=
        std::chrono::duration_cast<std::chrono::microseconds>(t4 - t3).count();

    std::string result = "Rejected";
    bool improved = false; // Track whether this iteration improved

    if (!s_new.isFeasible()) {
      destroyPool.scores[destroy_op_idx] += config.scoreIdentical;
      repairPool.scores[repair_op_idx] += config.scoreIdentical;
      // Không đếm infeasible vào stagnation — tránh perturbation quá sớm
      goto end_iteration;
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

    // ⭐ BUG FIX: Re-check feasibility AFTER LocalSearch.
    // LocalSearch (e.g., route merging/vehicle reduction) can make the
    // solution infeasible by losing customers. Without this re-check,
    // an infeasible solution (e.g., 1 vehicle for 100 customers) can
    // enter the Pareto archive and dominate everything.
    s_new.evaluateRoutes();
    if (!s_new.isFeasible()) {
      goto end_iteration;
    }

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

      // weightVectors: wIdx and w already computed at top of loop iteration.

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
      // totalStagnationEver_ = 0;
    } else {
      iterationsWithoutImprovement++;
      totalStagnationEver_++;
    }

    t6 = now();
    stats.acceptance_us +=
        std::chrono::duration_cast<std::chrono::microseconds>(t6 - t5).count();

    logger->logEvolutionStep(i, destroy_op->getName(), repair_op->getName(),
                             result, s_new);

  end_iteration:
    currentTemperature *= config.coolingRate;
    if (currentTemperature < config.minTemperature) {
      currentTemperature = config.minTemperature;
    }

    // ⭐ PERTURBATION MECHANISM: Escape local optima when stagnated
    // Phase 2: Strong perturbation at 1000+ iterations (check BEFORE Phase 1)
    if (iterationsWithoutImprovement > 0 &&
        iterationsWithoutImprovement % 1000 == 0) {
      // Aggressive reheating - HARD SET rather than max()
      currentTemperature = config.startTemperature * 0.5;

      // Jump to BEST solution from Pareto archive for diversification (min veh,
      // then min dist)
      auto &front = archive.getFront();
      if (!front.empty()) {
        const Solution *bestSol = nullptr;
        int minVeh = INT_MAX;
        double minDist = 1e18;
        for (const auto &sol : front) {
          int v = sol.getTotalVehicles();
          double d = sol.getTotalDistance();
          if (v < minVeh || (v == minVeh && d < minDist)) {
            minVeh = v;
            minDist = d;
            bestSol = &sol;
          }
        }
        if (bestSol)
          s_current = *bestSol;

        std::cout << "[Perturbation] Iter " << i
                  << ": Jumping to BEST archive solution [" << minVeh
                  << " veh, " << minDist << " dist] (stagnated "
                  << iterationsWithoutImprovement << " iters), Temp reset to "
                  << currentTemperature << std::endl;
      }
      // Reset counter so next phase boundary fires fresh after jump
      iterationsWithoutImprovement = 0;
    }
    // Phase 1: Mild perturbation (only fires at 500-iters, NOT at 1000-iters)
    else if (iterationsWithoutImprovement > 0 &&
             iterationsWithoutImprovement % 500 == 0 &&
             totalStagnationEver_ < 3000) {
      currentTemperature =
          std::min(currentTemperature * 1.25, config.startTemperature * 0.9);
    }

    // Adaptive RegretK noise reduction based on stagnation progress
    double maxStagnation =
        config.hvStagnationLimit * config.segmentIterations * 0.5;
    double progress = std::min(1.0, totalStagnationEver_ / maxStagnation);
    double currentNoise = config.noiseParameter * (1.0 - progress);

    // Scan repairPool for RegretK and update noise
    for (auto op : repairPool.operators) {
      if (auto regretK = std::dynamic_pointer_cast<RegretKRepair>(op)) {
        regretK->setNoiseParameter(currentNoise);
      }
    }

    // Phase 3: Very strong perturbation at 2000+ iterations - increase destroy
    // intensity. Dùng totalStagnationEver_ vì counter kia bị reset bởi Phase 2
    if (totalStagnationEver_ > 4000) {
      perturbationBoost_ = 1.5; // Destroy 50% more nodes
    } else if (totalStagnationEver_ > 2000) {
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
        if (bestSol) {
          // Chỉ reset stagnation ngắn hạn nếu thực sự jump sang solution khác
          if (std::abs(bestSol->getTotalDistance() -
                       s_current.getTotalDistance()) > 1e-6 ||
              bestSol->getTotalVehicles() != s_current.getTotalVehicles()) {
            s_current = *bestSol;
            iterationsWithoutImprovement = 0;
          } else {
            s_current = *bestSol;
          }
        }
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

          // ⭐ BUG FIX: When archive has only 1 solution, HV can be 0.0
          // (e.g., single point has zero volume). In this case, we must
          // count it as stagnation instead of resetting every segment.
          if (previousHV_ == 0.0 && currentHV > 1e-9) {
            // First non-zero HV calculation after a vehicle drop reset
            hvImprovement = 1.0;
            previousHV_ = currentHV;
            hvStagnationCount_ = 0;
          } else if (currentHV <= 1e-9) {
            // Archive has effectively 0 HV (1 solution or degenerate)
            // → counts as stagnation
            hvStagnationCount_++;
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
                    << ", bestDist=" << std::setprecision(2) << archiveBestDist
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
    // Wire weight hint for AdaptiveInsertion in mini-loop too
    if (auto adaptive = std::dynamic_pointer_cast<AdaptiveInsertion>(r_op)) {
      adaptive->setWeightHint(0.33, 0.33, 0.34); // Balanced in mini-loop
    }
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
      double baseDist = std::max(1.0, s_imp.getTotalDistance());
      double veh_penalty = std::max(500.0, baseDist * 3.0);
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
  std::cout << "[Info] Generating Initial Solution (Multi-start)...\n";
  Solution bestSol(instance);
  double bestCost = std::numeric_limits<double>::infinity();
  int minVehicles = std::numeric_limits<int>::max();

  std::vector<std::vector<int>> orderings;
  orderings.push_back(generateSweepOrder());
  orderings.push_back(generateNNOrder());
  orderings.push_back(generateEDFOrder());
  orderings.push_back(generateTightestTWOrder());

  std::string orderNames[] = {"Sweep", "True Nearest Neighbor",
                              "Earliest Deadline First",
                              "Tightest Time Window First"};

  for (size_t i = 0; i < orderings.size(); ++i) {
    std::cout << "  -> Trying heuristic: " << orderNames[i] << "...\n";
    Solution s = constructSolutionFromOrder(orderings[i]);

    if (!s.isFeasible()) {
      std::cout << "     Failed (Infeasible)\n";
      continue;
    }

    int v = s.getTotalVehicles();
    double dist = s.getTotalDistance();
    std::cout << "     Result: " << v << " vehicles, dist " << dist << "\n";

    if (v < minVehicles || (v == minVehicles && dist < bestCost)) {
      minVehicles = v;
      bestCost = dist;
      bestSol = s;
    }
  }

  if (minVehicles == std::numeric_limits<int>::max()) {
    std::cerr << "[ERROR] Could not find any feasible initial solution!\n";
  } else {
    std::cout << "\n========================================\n"
              << "Best Initial Solution Summary:\n"
              << "  Routes: " << bestSol.getNumRoutes() << "\n"
              << "  Total Distance: " << bestSol.getTotalDistance() << "\n"
              << "========================================\n\n";
  }

  return bestSol;
}

std::vector<int> ALNSSolver::generateSweepOrder() {
  std::vector<int> unservedIds;
  auto depotNode = instance->getNodeById(0);
  double depotX = depotNode->getX();
  double depotNodeY = depotNode->getY();

  for (const auto &c : instance->getCustomers())
    unservedIds.push_back(c->getId());

  std::sort(unservedIds.begin(), unservedIds.end(), [&](int a_id, int b_id) {
    auto a = instance->getNodeById(a_id);
    auto b = instance->getNodeById(b_id);
    double angleA = std::atan2(a->getY() - depotNodeY, a->getX() - depotX);
    double angleB = std::atan2(b->getY() - depotNodeY, b->getX() - depotX);
    if (std::abs(angleA - angleB) < 1e-6) {
      return instance->getDistance(0, a_id) > instance->getDistance(0, b_id);
    }
    return angleA < angleB;
  });
  return unservedIds;
}

std::vector<int> ALNSSolver::generateNNOrder() {
  std::vector<int> allCustomerIds;
  for (const auto &c : instance->getCustomers())
    allCustomerIds.push_back(c->getId());

  std::vector<int> nnOrdering;
  // Max ID lookup
  int maxId = 0;
  for (const auto &node : instance->getNodes())
    maxId = std::max(maxId, node->getId());

  std::vector<bool> visited(maxId + 1, false);
  int current = 0; // Depot

  while (nnOrdering.size() < allCustomerIds.size()) {
    int nearest = -1;
    double minDist = 1e18;
    for (int cid : allCustomerIds) {
      if (!visited[cid]) {
        double d = instance->getDistance(current, cid);
        if (d < minDist) {
          minDist = d;
          nearest = cid;
        }
      }
    }
    nnOrdering.push_back(nearest);
    visited[nearest] = true;
    current = nearest;
  }
  return nnOrdering;
}

std::vector<int> ALNSSolver::generateEDFOrder() {
  std::vector<int> unservedIds;
  for (const auto &c : instance->getCustomers())
    unservedIds.push_back(c->getId());

  std::sort(unservedIds.begin(), unservedIds.end(), [&](int a_id, int b_id) {
    auto a = instance->getNodeById(a_id);
    auto b = instance->getNodeById(b_id);
    return a->getDueDate() < b->getDueDate();
  });
  return unservedIds;
}

std::vector<int> ALNSSolver::generateTightestTWOrder() {
  std::vector<int> unservedIds;
  for (const auto &c : instance->getCustomers())
    unservedIds.push_back(c->getId());

  std::sort(unservedIds.begin(), unservedIds.end(), [&](int a_id, int b_id) {
    auto a = instance->getNodeById(a_id);
    auto b = instance->getNodeById(b_id);
    double twA = a->getDueDate() - a->getReadyTime();
    double twB = b->getDueDate() - b->getReadyTime();
    if (std::abs(twA - twB) < 1e-6) {
      return a->getDueDate() < b->getDueDate();
    }
    return twA < twB;
  });
  return unservedIds;
}

Solution ALNSSolver::constructSolutionFromOrder(
    const std::vector<int> &orderedCustomerIds) {
  Solution sol(instance);

  int maxId = 0;
  for (const auto &node : instance->getNodes())
    maxId = std::max(maxId, node->getId());

  std::vector<Customer *> custLookup(maxId + 1, nullptr);
  std::vector<Station *> stationLookup(maxId + 1, nullptr);
  std::vector<int> stationIds;

  for (const auto &c : instance->getCustomers())
    custLookup[c->getId()] = c.get();
  for (const auto &s : instance->getStations()) {
    stationLookup[s->getId()] = s.get();
    stationIds.push_back(s->getId());
  }

  auto depotNode = instance->getNodeById(0);
  const double maxTime = depotNode->getDueDate();
  const double vehCapacity = instance->getVehicleCapacity();
  const double vehMaxBattery = instance->getVehicleBattery();
  const double vehEnergyRate = instance->getVehicleEnergyRate();
  const int depotId = 0;

  std::vector<int> unservedIds = orderedCustomerIds;

  bool cannotServeMore = false;
  int globalRouteAttempts = 0;
  const int MAX_ROUTE_ATTEMPTS = (int)unservedIds.size() * 5 + 50;

  while (!unservedIds.empty() && !cannotServeMore &&
         globalRouteAttempts < MAX_ROUTE_ATTEMPTS) {
    ++globalRouteAttempts;

    auto vehicle = std::make_shared<Vehicle>(sol.getNumRoutes(), vehCapacity,
                                             vehMaxBattery, vehEnergyRate);
    Route currentRoute(sol.getNumRoutes(), vehicle, instance);

    int currNodeId = depotId;
    double currTime = 0.0;
    double currBatt = vehMaxBattery;
    double currLoad = 0.0;

    bool routeFinished = false;
    bool customerAddedInThisRoute = false;

    while (!routeFinished) {
      int bestCustId = -1;
      double bestScore = std::numeric_limits<double>::max();

      // ⭐ FIX: Weighted Scoring (Rank-based)
      // Iterate through unservedIds to find the best fit, but heavily penalize
      // skipping the order.
      for (size_t i = 0; i < unservedIds.size(); ++i) {
        int custId = unservedIds[i];
        Customer *cust = custLookup[custId];

        if (currLoad + cust->getDemand() > vehCapacity)
          continue;

        double dist = instance->getDistance(currNodeId, custId);
        double energyNeeded = dist * vehEnergyRate;
        if (currBatt < energyNeeded)
          continue;

        double travelTime = instance->getTime(currNodeId, custId);
        double arrivalTime = currTime + travelTime;

        if (arrivalTime > cust->getDueDate())
          continue;

        double startService = std::max(arrivalTime, cust->getReadyTime());
        double endService = startService + cust->getServiceTime();
        double energyLeftAtCust = currBatt - energyNeeded;

        double distToDepot = instance->getDistance(custId, depotId);
        double energyToDepot = distToDepot * vehEnergyRate;
        double timeToDepot = instance->getTime(custId, depotId);

        bool safeReturn = false;

        if (energyLeftAtCust >= energyToDepot &&
            (endService + timeToDepot <= maxTime)) {
          safeReturn = true;
        } else {
          int nearStationId = instance->getNearestStationId(custId);
          if (nearStationId != -1) {
            double distToStat = instance->getDistance(custId, nearStationId);
            double energyToStat = distToStat * vehEnergyRate;

            if (energyLeftAtCust >= energyToStat) {
              double timeToStat = instance->getTime(custId, nearStationId);
              Station *stat = stationLookup[nearStationId];
              double arrivalStat = endService + timeToStat;
              double chargeNeeded =
                  vehMaxBattery - (energyLeftAtCust - energyToStat);
              double chargeTime = chargeNeeded * stat->getChargingRate();
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

        // Scoring: Distance + Urgency + RANK PENALTY
        // Rank penalty ensures we mostly respect the order, but can skip
        // a truly terrible fit.
        double urgency = 1.0 / (std::max(1.0, cust->getDueDate() - currTime));
        double rankPenalty = i * 1000.0; // Huge penalty for skipping
        double score = dist - (urgency * 100.0) + rankPenalty;

        if (score < bestScore) {
          bestScore = score;
          bestCustId = custId;

          // Optimization: If we found the very first one feasible, take it
          // immediately
          if (i == 0)
            break;
        }
      }

      if (bestCustId != -1) {
        currentRoute.addNode(bestCustId, currentRoute.getNodes().size() - 1);
        currentRoute.evaluate();

        if (!currentRoute.isFeasible()) {
          currentRoute.removeNode(currentRoute.getNodes().size() - 2);
          currentRoute.evaluate();
          routeFinished = true;
          continue;
        }

        Customer *cust = custLookup[bestCustId];
        const auto &states = currentRoute.getStates();
        if (!states.empty()) {
          const auto &lastState = states[states.size() - 2];
          currTime = lastState.departureTime;
          currBatt = lastState.remainingBattery;
          currLoad += cust->getDemand();
        }
        currNodeId = bestCustId;

        for (size_t i = 0; i < unservedIds.size(); ++i) {
          if (unservedIds[i] == bestCustId) {
            unservedIds.erase(unservedIds.begin() + i);
            break;
          }
        }
        customerAddedInThisRoute = true;
      } else {
        // ⭐ FIX: Always try charging if stuck, regardless of battery level
        // (unless full)
        if (currBatt < vehMaxBattery - 1e-6) {
          int bestStationId = -1;
          double minStationDist = std::numeric_limits<double>::max();

          for (int sid : stationIds) {
            double d = instance->getDistance(currNodeId, sid);
            double e = d * vehEnergyRate;

            if (currBatt >= e) {
              double tToStat = instance->getTime(currNodeId, sid);
              Station *stat = stationLookup[sid];

              double distToDepot = instance->getDistance(sid, depotId);
              double energyToDepot = distToDepot * vehEnergyRate;
              double targetBatt = std::min(energyToDepot * 1.1, vehMaxBattery);

              double battAtStation = currBatt - e;
              double compromiseTarget =
                  std::max(targetBatt, vehMaxBattery * 0.6);
              compromiseTarget = std::min(compromiseTarget, vehMaxBattery);

              double chargeAmount =
                  std::max(0.0, compromiseTarget - battAtStation);
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
            currentRoute.addNode(bestStationId,
                                 currentRoute.getNodes().size() - 1);
            currentRoute.evaluate();

            if (!currentRoute.isFeasible()) {
              currentRoute.removeNode(currentRoute.getNodes().size() - 2);
              currentRoute.evaluate();
              routeFinished = true;
              continue;
            }

            Station *stat = stationLookup[bestStationId];
            double d = instance->getDistance(currNodeId, bestStationId);
            double e = d * vehEnergyRate;
            double t = instance->getTime(currNodeId, bestStationId);

            double distToDepot = instance->getDistance(bestStationId, depotId);
            double energyToDepot = distToDepot * vehEnergyRate;
            double targetBatt = std::min(energyToDepot * 1.1, vehMaxBattery);
            double compromiseTarget = std::min(
                std::max(targetBatt, vehMaxBattery * 0.6), vehMaxBattery);

            double battAtStation = currBatt - e;
            double chargeAmount =
                std::max(0.0, compromiseTarget - battAtStation);
            double chargeTime = chargeAmount * stat->getChargingRate();

            currBatt = battAtStation + chargeAmount;
            currTime += t + chargeTime;
            currNodeId = bestStationId;
            continue;
          }
        }
        routeFinished = true;
      }
    }

    if (customerAddedInThisRoute) {
      currentRoute.evaluate();
      if (currentRoute.isFeasible()) {
        sol.addRoute(currentRoute);
      } else {
        for (int custId : currentRoute.getCustomers()) {
          unservedIds.push_back(custId);
        }
      }
    } else {
      cannotServeMore = true;
    }
  }

  // Fallback -> Route riêng lẻ
  if (!unservedIds.empty()) {
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
        bool repaired = false;

        for (int stationId : stationIds) {
          Route rAfter = singleRoute;
          rAfter.addNode(stationId, 2);
          rAfter.evaluate();
          if (rAfter.isFeasible()) {
            sol.addRoute(rAfter);
            repaired = true;
            break;
          }

          Route rBefore = singleRoute;
          rBefore.addNode(stationId, 1);
          rBefore.evaluate();
          if (rBefore.isFeasible()) {
            sol.addRoute(rBefore);
            repaired = true;
            break;
          }
        }

        if (!repaired) {
          std::vector<std::pair<double, int>> sortedStations;
          sortedStations.reserve(stationIds.size());
          for (int sid : stationIds) {
            sortedStations.push_back({instance->getDistance(custId, sid), sid});
          }
          std::sort(sortedStations.begin(), sortedStations.end());
          int topK = std::min((int)sortedStations.size(), 4);

          for (int k1 = 0; k1 < topK && !repaired; ++k1) {
            int s1 = sortedStations[k1].second;
            for (int k2 = 0; k2 < topK && !repaired; ++k2) {
              if (k1 == k2)
                continue;
              int s2 = sortedStations[k2].second;
              Route routeDouble = singleRoute;
              routeDouble.addNode(s1, 1);
              routeDouble.addNode(s2, 3);
              routeDouble.evaluate();
              if (routeDouble.isFeasible()) {
                sol.addRoute(routeDouble);
                repaired = true;
              }
            }
          }
        }

        if (!repaired)
          stillUnserved.push_back(custId);
      }
    }
    unservedIds = stillUnserved;
  }

  sol.evaluateRoutes();
  return sol;
}

} // namespace alns
