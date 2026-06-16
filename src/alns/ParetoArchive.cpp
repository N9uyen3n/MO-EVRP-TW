#include "../../include/alns/ParetoArchive.h"
#include <algorithm>
#include <cmath> // For std::abs
#include <iostream>
#include <limits>
#include <numeric> // For std::iota
#include <stdexcept>

ParetoArchive::ParetoArchive(int maxSize) : maxSize(maxSize) {
  if (maxSize <= 0) {
    throw std::invalid_argument("ParetoArchive max size must be positive.");
  }
}

size_t ParetoArchive::getSize() const { return getFront().size(); }

const std::vector<Solution> &ParetoArchive::getFront() const {
  flatArchiveCache_.clear();

  // To ensure the global front maintains strict hierarchical dominance,
  // we first find the minimum number of vehicles currently in the archive.
  int minVehicles = std::numeric_limits<int>::max();
  for (const auto &kv : archiveByVehicles_) {
    if (kv.first < minVehicles) {
      minVehicles = kv.first;
    }
  }

  // Iterate over all buckets and only add a solution if it's NOT dominated by
  // ANY solution in a bucket with strictly fewer vehicles.
  for (const auto &kv : archiveByVehicles_) {
    int currentVeh = kv.first;
    const auto &vec = kv.second;

    for (const auto &sol : vec) {
      bool isDominatedGlobally = false;

      // Check against all buckets with fewer vehicles
      for (const auto &lowerKv : archiveByVehicles_) {
        int lowerVeh = lowerKv.first;
        if (lowerVeh >= currentVeh)
          break; // archiveByVehicles_ is std::map, so it's ordered by key

        for (const auto &betterSol : lowerKv.second) {
          if (betterSol.dominates(sol)) {
            isDominatedGlobally = true;
            break;
          }
        }
        if (isDominatedGlobally)
          break;
      }

      if (!isDominatedGlobally) {
        flatArchiveCache_.push_back(sol);
      }
    }
  }

  return flatArchiveCache_;
}

Solution ParetoArchive::getRandomSolution(std::mt19937 &rng) const {
  const auto &flat = getFront();
  if (flat.empty()) {
    throw std::runtime_error(
        "Cannot get random solution: Pareto archive is empty.");
  }
  std::uniform_int_distribution<> dist(0, static_cast<int>(flat.size() - 1));
  return flat[dist(rng)];
}

AddResult ParetoArchive::tryAdd(const Solution &newSolution) {
  expandNadirIfNeeded(newSolution);
  int veh = newSolution.getTotalVehicles();

  // Cross-bucket strict dominance check:
  // If there is ANY solution in the archive with FEWER vehicles, this new
  // solution is globally dominated and should not be added.
  if (!archiveByVehicles_.empty()) {
    int minVehicles = archiveByVehicles_.begin()->first;
    if (minVehicles < veh) {
      return AddResult::DOMINATED;
    }
  }

  auto &bucket = archiveByVehicles_[veh];

  bool isDominated = false;
  std::vector<int> dominatedIndices;
  const double EPSILON = 1e-6;

  for (size_t i = 0; i < bucket.size(); ++i) {
    const auto &existingSolution = bucket[i];

    // Check for duplicates
    bool sameDist = std::abs(existingSolution.getTotalDistance() -
                             newSolution.getTotalDistance()) < EPSILON;
    bool sameWorkload = std::abs(existingSolution.getWorkloadGini() -
                                 newSolution.getWorkloadGini()) < EPSILON;
    bool sameMaxTime = std::abs(existingSolution.getMaxTime() -
                                newSolution.getMaxTime()) < EPSILON;

    if (sameDist && sameWorkload && sameMaxTime) {
      return AddResult::IDENTICAL;
    }

    // Since they have the same number of vehicles, we just check Pareto
    // dominance on the remaining objectives (Distance, Gini, MaxTime). Both
    // solutions evaluate to 'false' on the totalVehicles difference in
    // Solution::dominates.
    if (existingSolution.dominates(newSolution)) {
      isDominated = true;
      break;
    }

    if (newSolution.dominates(existingSolution)) {
      dominatedIndices.push_back(static_cast<int>(i));
    }
  }

  if (isDominated) {
    return AddResult::DOMINATED;
  }

  // Remove dominated existing solutions (iterate backwards to preserve indices)
  std::sort(dominatedIndices.rbegin(), dominatedIndices.rend());
  for (int index : dominatedIndices) {
    bucket.erase(bucket.begin() + index);
  }

  // Add the new solution
  bucket.push_back(newSolution);

  // Prune if THIS BUCKET exceeds its maximum size
  if (bucket.size() > static_cast<size_t>(maxSize)) {
    prune(veh);
  }

  if (!dominatedIndices.empty()) {
    // Cross-bucket pruning: Since this new solution is non-dominated (and
    // possibly dominating others in its bucket), and we have a strict
    // hierarchical objective on vehicles, we should remove ALL solutions that
    // have MORE vehicles than this new solution, because they are automatically
    // dominated by it.
    std::vector<int> bucketsToRemove;
    for (const auto &kv : archiveByVehicles_) {
      if (kv.first > veh) {
        bucketsToRemove.push_back(kv.first);
      }
    }
    for (int v : bucketsToRemove) {
      archiveByVehicles_.erase(v);
    }
    return AddResult::DOMINATING;
  } else {
    // Even if it's just non-dominated in its own bucket, it still strictly
    // dominates everything in buckets with MORE vehicles.
    std::vector<int> bucketsToRemove;
    for (const auto &kv : archiveByVehicles_) {
      if (kv.first > veh) {
        bucketsToRemove.push_back(kv.first);
      }
    }
    for (int v : bucketsToRemove) {
      archiveByVehicles_.erase(v);
    }
    return AddResult::NON_DOMINATED;
  }
}

Solution ParetoArchive::getBestSolutionForObjective(int objectiveIndex) const {
  const auto &flat = getFront();
  if (flat.empty())
    throw std::runtime_error("Archive is empty");

  // Comparator lambda based on the corrected objectives
  auto compareFn = [&](const Solution &a, const Solution &b) {
    switch (objectiveIndex) {
    case 0:
      return a.getTotalDistance() < b.getTotalDistance(); // Objective: Distance
    case 1:
      return a.getWorkloadGini() < b.getWorkloadGini(); // Objective: Workload Variance
    case 2:
      return a.getMaxTime() < b.getMaxTime(); // Objective: Max Time
    case 3:
      return a.getTotalVehicles() < b.getTotalVehicles(); // Objective: Vehicles
    default:
      return a.getTotalDistance() < b.getTotalDistance();
    }
  };

  return *std::min_element(flat.begin(), flat.end(), compareFn);
}

// Pruning function using Crowding Distance with the corrected objectives
// Now prunes ONLY within a specific vehicle bucket
void ParetoArchive::prune(int veh) {
  auto &bucket = archiveByVehicles_[veh];
  size_t n = bucket.size();
  if (n <= static_cast<size_t>(maxSize))
    return;

  // Objectives for crowding distance: Distance, Workload Variance, Max Time
  // (We omit Vehicles because they are all the same in this bucket)
  const int numObjectives = 3;
  std::vector<double> crowdingDistances(n, 0.0);
  std::vector<int> indices(n);
  std::iota(indices.begin(), indices.end(), 0);

  for (int m = 0; m < numObjectives; ++m) {
    // Sort by the current objective
    auto sortFn = [&](int a, int b) {
      switch (m) {
      case 0:
        return bucket[a].getTotalDistance() < bucket[b].getTotalDistance();
      case 1:
        return bucket[a].getWorkloadGini() < bucket[b].getWorkloadGini();
      case 2:
        return bucket[a].getMaxTime() < bucket[b].getMaxTime();
      default:
        return false;
      }
    };
    std::sort(indices.begin(), indices.end(), sortFn);

    // Assign infinite distance to the boundary points to always keep them
    crowdingDistances[indices[0]] = std::numeric_limits<double>::infinity();
    crowdingDistances[indices[n - 1]] = std::numeric_limits<double>::infinity();

    // Get min/max values for the current objective
    double val_min, val_max;
    switch (m) {
    case 0:
      val_min = bucket[indices[0]].getTotalDistance();
      val_max = bucket[indices[n - 1]].getTotalDistance();
      break;
    case 1:
      val_min = bucket[indices[0]].getWorkloadGini();
      val_max = bucket[indices[n - 1]].getWorkloadGini();
      break;
    case 2:
      val_min = bucket[indices[0]].getMaxTime();
      val_max = bucket[indices[n - 1]].getMaxTime();
      break;
    default:
      val_min = 0;
      val_max = 1;
      break; // Should not happen
    }

    double range = val_max - val_min;
    if (range < 1e-9)
      range = 1.0; // Avoid division by zero

    // Add to the crowding distance
    for (size_t i = 1; i < n - 1; ++i) {
      if (crowdingDistances[indices[i]] ==
          std::numeric_limits<double>::infinity())
        continue;

      double prevVal, nextVal;
      switch (m) {
      case 0:
        prevVal = bucket[indices[i - 1]].getTotalDistance();
        nextVal = bucket[indices[i + 1]].getTotalDistance();
        break;
      case 1:
        prevVal = bucket[indices[i - 1]].getWorkloadGini();
        nextVal = bucket[indices[i + 1]].getWorkloadGini();
        break;
      case 2:
        prevVal = bucket[indices[i - 1]].getMaxTime();
        nextVal = bucket[indices[i + 1]].getMaxTime();
        break;
      default:
        prevVal = 0;
        nextVal = 0;
        break; // Should not happen
      }
      crowdingDistances[indices[i]] += (nextVal - prevVal) / range;
    }
  }

  // Find the element with the smallest crowding distance (densest region) to
  // remove
  int worstIndex = static_cast<int>(
      std::min_element(crowdingDistances.begin(), crowdingDistances.end()) -
      crowdingDistances.begin());
  bucket.erase(bucket.begin() + worstIndex);

  if (bucket.empty()) {
    archiveByVehicles_.erase(veh);
  }
}

void ParetoArchive::clear() {
  archiveByVehicles_.clear();
  flatArchiveCache_.clear();
}

double ParetoArchive::computeHypervolume() const {
  const auto &flat = getFront();
  if (flat.empty())
    return 0.0;

  // =========================================================================
  // 3D Hypervolume via Slicing (Distance × Gini × MaxTime)
  // Algorithm: Sort points by MaxTime, sweep slices, accumulate 2D HV per slice
  // Reference: Beume et al. (2009), "S-Metric Computation via Slicing"
  // =========================================================================
  const double REF = 1.1; // Reference point after normalization (per axis)

  double usedIdealDist = refIdealDist_;
  double usedIdealGini = refIdealGini_;
  double usedIdealTime = refIdealMaxTime_;

  double usedNadirDist = refNadirDist_;
  double usedNadirGini = refNadirGini_;
  double usedNadirTime = refNadirMaxTime_;

  // Ensure minimum range to avoid division by zero
  double rangeDist = std::max(1e-6, usedNadirDist - usedIdealDist);
  double rangeGini = std::max(1e-6, usedNadirGini - usedIdealGini);
  double rangeTime = std::max(1e-6, usedNadirTime - usedIdealTime);

  // Normalize all points to [0, 1] using fixed ideal/nadir
  struct Point3D {
    double dist;    // normalized
    double gini;    // normalized
    double maxTime; // normalized
  };

  // Find min vehicles of current front to only compute HV for the lowest
  // vehicle level
  int minVeh = 1e9;
  for (const auto &sol : flat) {
    minVeh = std::min(minVeh, sol.getTotalVehicles());
  }

  std::vector<Point3D> points;
  points.reserve(flat.size());
  for (const auto &sol : flat) {
    // Only compute HV for the current lowest vehicle level
    if (sol.getTotalVehicles() > minVeh)
      continue;
 
    double nD = (sol.getTotalDistance() - usedIdealDist) / rangeDist;
    double nG = (sol.getWorkloadGini() - usedIdealGini) / rangeGini;
    double nT = (sol.getMaxTime() - usedIdealTime) / rangeTime;
 
    // Clamp to reference point (solutions worse than reference slack are
    // discarded or clamped)
    if (nD < REF && nG < REF && nT < REF) {
      points.push_back(
          {std::max(0.0, nD), std::max(0.0, nG), std::max(0.0, nT)});
    }
  }

  if (points.empty()) {
      return 0.0;
  }

  // Sort by MaxTime ascending (slicing axis)
  std::sort(
      points.begin(), points.end(),
      [](const Point3D &a, const Point3D &b) { return a.maxTime < b.maxTime; });

  // 3D HV by slicing: for each unique MaxTime level, compute the 2D HV
  // of all points with MaxTime <= that level, times the slice thickness.
  double hv3d = 0.0;

  // Active set: points accumulated so far (for 2D front of dist × gini)
  std::vector<Point3D> activePoints;

  for (size_t idx = 0; idx < points.size(); ++idx) {
    activePoints.push_back(points[idx]);

    // Determine slice thickness (from this MaxTime to the next, or to REF)
    double sliceEnd;
    if (idx + 1 < points.size()) {
      sliceEnd = points[idx + 1].maxTime;
    } else {
      sliceEnd = REF;
    }
    double sliceThickness = sliceEnd - points[idx].maxTime;
    if (sliceThickness <= 1e-12)
      continue;

    // Compute 2D HV of activePoints on (gini, dist) plane
    // Sort by gini ascending
    std::vector<std::pair<double, double>> pts2d; // (gini, dist)
    pts2d.reserve(activePoints.size());
    for (const auto &p : activePoints) {
      pts2d.push_back({p.gini, p.dist});
    }
    std::sort(pts2d.begin(), pts2d.end());

    // Extract 2D non-dominated front
    std::vector<std::pair<double, double>> front2d;
    double minDist = REF + 1.0;
    for (const auto &p : pts2d) {
      if (p.second < minDist) {
        front2d.push_back(p);
        minDist = p.second;
      }
    }

    // Sweep-line 2D HV
    double hv2d = 0.0;
    for (size_t i = 0; i < front2d.size(); ++i) {
      double width;
      if (i + 1 < front2d.size()) {
        width = front2d[i + 1].first - front2d[i].first;
      } else {
        width = REF - front2d[i].first;
      }
      double height = REF - front2d[i].second;
      hv2d += width * height;
    }

    hv3d += hv2d * sliceThickness;
  }

  return hv3d;
}

double ParetoArchive::computeHypervolume2D() const {
  const auto &flat = getFront();
  if (flat.empty())
    return 0.0;

  // =========================================================================
  // 2D Hypervolume (Distance × Gini)
  // Algorithm: Sweep-line (Standard 2D HV)
  // =========================================================================
  const double REF = 1.1; // Reference point after normalization

  double usedIdealDist = refIdealDist_;
  double usedIdealGini = refIdealGini_;
  double usedNadirDist = refNadirDist_;
  double usedNadirGini = refNadirGini_;

  double rangeDist = std::max(1e-6, usedNadirDist - usedIdealDist);
  double rangeGini = std::max(1e-6, usedNadirGini - usedIdealGini);

  // Normalize all points to [0, 1] using fixed ideal/nadir
  struct Point2D {
    double dist; // normalized
    double gini; // normalized
  };

  // Find min vehicles of current front to only compute HV for the lowest
  // vehicle level (preserving existing hierarchy logic)
  int minVeh = 1e9;
  for (const auto &sol : flat) {
    minVeh = std::min(minVeh, sol.getTotalVehicles());
  }

  std::vector<Point2D> points;
  points.reserve(flat.size());
  for (const auto &sol : flat) {
    if (sol.getTotalVehicles() > minVeh)
      continue;

    double nD = (sol.getTotalDistance() - usedIdealDist) / rangeDist;
    double nG = (sol.getWorkloadGini() - usedIdealGini) / rangeGini;

    if (nD < REF && nG < REF) {
      points.push_back({std::max(0.0, nD), std::max(0.0, nG)});
    }
  }

  if (points.empty())
    return 0.0;

  // Sort by Gini ascending
  std::sort(points.begin(), points.end(),
            [](const Point2D &a, const Point2D &b) {
              if (std::abs(a.gini - b.gini) > 1e-12)
                return a.gini < b.gini;
              return a.dist < b.dist;
            });

  // Extract 2D non-dominated front (just in case, although getFront() should handle it)
  std::vector<Point2D> front2d;
  double minDist = REF + 1.0;
  for (const auto &p : points) {
    if (p.dist < minDist - 1e-12) {
      front2d.push_back(p);
      minDist = p.dist;
    }
  }

  // Sweep-line 2D HV area calculation
  double hv2d = 0.0;
  for (size_t i = 0; i < front2d.size(); ++i) {
    double width;
    if (i + 1 < front2d.size()) {
      width = front2d[i + 1].gini - front2d[i].gini;
    } else {
      width = REF - front2d[i].gini;
    }
    double height = REF - front2d[i].dist;
    hv2d += width * height;
  }

  return hv2d;
}


// Fixed Reference Box System — Freezes normalization bounds per vehicle level.
// This prevents HV from artificially jumping/dropping due to box scaling when
// extreme solutions arrive.
void ParetoArchive::initializeReferenceBox(const Solution &initialSolution) {
  // Ideal: lower bounds
  refIdealDist_ = 0.0;
  refIdealGini_ = 0.0;
  refIdealMaxTime_ = 0.0;

  // Nadir: upper bounds from initial solution
  refNadirDist_ = initialSolution.getTotalDistance() * 1.5; // Fixed upper boundary logic
  refNadirGini_ = 1.0;                          // Gini bounded by definition
  refNadirMaxTime_ = initialSolution.getMaxTime() * 1.5;
}

void ParetoArchive::expandNadirIfNeeded(const Solution &sol) {
  double d = sol.getTotalDistance();
  double g = sol.getWorkloadGini();
  double t = sol.getMaxTime();
  if (d * 1.1 > refNadirDist_)    refNadirDist_    = d * 1.2;
  if (g * 1.1 > refNadirGini_)    refNadirGini_    = g * 1.2;
  if (t * 1.1 > refNadirMaxTime_) refNadirMaxTime_ = t * 1.2;
}
