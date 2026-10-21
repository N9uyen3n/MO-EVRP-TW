#pragma once
#include "../core/Solution.h"
#include <vector>

namespace alns::search {
// Branch default is NoEQ, including builds outside CMake. CMake OFF restores FP-PALNS.
#ifndef ALNS_NO_EQUITY
#define ALNS_NO_EQUITY 1
#endif
inline constexpr bool equityEnabled = ALNS_NO_EQUITY == 0;
inline constexpr const char* variant = equityEnabled ? "FP-PALNS" : "FP-PALNS-NoEQ";
inline constexpr const char* archiveObjectives = equityEnabled ? "Z2,Z3,Z4" : "Z2,Z4";

// Gini remains a real reported objective; only search decisions use this projection.
inline double gini(const Solution& s) {
  if constexpr (equityEnabled) return s.getWorkloadGini();
  return 0.0;
}
inline bool dominates(const Solution& a, const Solution& b) {
  if constexpr (equityEnabled) return a.dominates(b);
  if (a.getTotalVehicles() != b.getTotalVehicles())
    return a.getTotalVehicles() < b.getTotalVehicles();
  return a.getTotalDistance() <= b.getTotalDistance() + 1e-4 &&
         a.getMaxTime() <= b.getMaxTime() + 1e-4 &&
         (a.getTotalDistance() < b.getTotalDistance() - 1e-4 ||
          a.getMaxTime() < b.getMaxTime() - 1e-4);
}
struct Weight { double dist, gini, time; };
inline Weight project(Weight w) {
  if constexpr (!equityEnabled) w.gini = 0.0;
  return w; // Keep inherited distance/time coefficients; no recalibration or rescaling.
}
inline const std::vector<Weight>& weights() {
  static const std::vector<Weight> result = [] {
    const std::vector<Weight> inherited = {
      {1,0,0}, {0,1,0}, {0,0,1}, {.5,.5,0}, {.3,.6,.1},
      {0,.6,.4}, {.6,.25,.15}, {.1,.65,.25}, {.2,.3,.5}, {.33,.34,.33}};
    std::vector<Weight> active;
    for (auto w : inherited) {
      w = project(w);
      if (w.dist + w.gini + w.time > 0.0) active.push_back(w);
    }
    return active;
  }();
  return result;
}
} // namespace alns::search
