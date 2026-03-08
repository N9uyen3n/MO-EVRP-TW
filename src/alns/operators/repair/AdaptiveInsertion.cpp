#include "../../../../include/alns/operators/repair/AdaptiveInsertion.h"
#include <algorithm>
#include <iostream>
#include <limits>
#include <random>


namespace alns {

AdaptiveInsertion::AdaptiveInsertion(std::shared_ptr<Instance> instance)
    : instance(instance) {}

std::string AdaptiveInsertion::getName() const { return "Adaptive Insertion"; }

void AdaptiveInsertion::setWeightHint(double dist, double gini, double time) {
  weightHint_ = {dist, gini, time};
}

void AdaptiveInsertion::execute(Solution &solution,
                                const std::vector<int> &unservedCustomers,
                                std::mt19937 &rng) {
  std::uniform_real_distribution<> randDist(0.0, 1.0);
  double r = randDist(rng);

  Mode mode;
  double distShare = weightHint_.dist;
  if (r < 0.3) {
    mode = Mode::VEHICLE_PACKING;
  } else if (distShare > 0.6) {
    mode = Mode::DISTANCE_FOCUSED;
  } else {
    double totalMO = weightHint_.gini + weightHint_.time;
    if (totalMO < 1e-9) {
      mode = Mode::DISTANCE_FOCUSED;
    } else {
      double r2 = (r - 0.3) / 0.7; // Re-scale r to [0, 1)
      double giniShare = weightHint_.gini / totalMO;
      mode = (r2 < giniShare) ? Mode::WORKLOAD_FOCUSED : Mode::MAXTIME_FOCUSED;
    }
  }

  std::vector<int> customers = unservedCustomers;
  // Sort by difficulty (Tighter Time Window first)
  std::sort(customers.begin(), customers.end(), [&](int a, int b) {
    auto nodeA = instance->getNodeById(a);
    auto nodeB = instance->getNodeById(b);
    double twA = nodeA->getDueDate() - nodeA->getReadyTime();
    double twB = nodeB->getDueDate() - nodeB->getReadyTime();
    if (std::abs(twA - twB) < 1e-6) {
      return nodeA->getDueDate() < nodeB->getDueDate();
    }
    return twA < twB;
  });

  auto &routes = solution.getRoutes();

  // Get time horizon from depot (node 0)
  double timeHorizon = instance->getNodeById(0)->getDueDate();
  if (timeHorizon < 1e-6)
    timeHorizon = 230.0; // fallback

  // Pre-calculate route statistics
  double totalDuration = 0.0;
  double maxRouteDuration = 0.0;
  double totalRouteDistance = 0.0;
  int activeRoutes = 0;

  for (size_t ri = 0; ri < routes.size(); ++ri) {
    const auto &route = routes[ri];
    if (!route.getCustomers().empty()) {
      double duration = route.getTotalTime();
      totalDuration += duration;
      totalRouteDistance += route.getTotalDistance();
      activeRoutes++;
      if (duration > maxRouteDuration) {
        maxRouteDuration = duration;
      }
    }
  }

  for (int customerId : customers) {
    double meanRouteDuration =
        activeRoutes > 0 ? totalDuration / activeRoutes : 0.0;
    double avgRouteDistance =
        activeRoutes > 0 ? totalRouteDistance / activeRoutes : 0.0;

    struct BestInsertion {
      int routeIdx = -1;
      int position = -1;
      double cost = std::numeric_limits<double>::infinity();
      bool usedStation = false;
      int stationId = -1;
      bool stationBefore = true;
    } bestInsertion;

    auto custNode = instance->getNodeById(customerId);
    double custDemand = custNode->getDemand();

    // ── Phase 1: Direct insertion into existing routes ────────────────
    for (size_t ri = 0; ri < routes.size(); ++ri) {
      auto &route = routes[ri];

      if (!route.quickCapacityCheck(custDemand))
        continue;

      const auto &nodes = route.getNodes();

      for (size_t i = 1; i < nodes.size(); ++i) {
        if (!route.canPossiblyInsert(customerId, i))
          continue;

        auto result = route.checkInsertionCost(customerId, i);

        if (result.isFeasible) {
          double cost = calculateCost(
              result, mode, route, meanRouteDuration, maxRouteDuration,
              customerId, static_cast<int>(i), avgRouteDistance, timeHorizon);
          if (cost < bestInsertion.cost) {
            bestInsertion = {static_cast<int>(ri),
                             static_cast<int>(i),
                             cost,
                             false,
                             -1,
                             true};
          }
        }
      }
    }

    // ── Phase 2: Station-Assisted for PACKING mode ───────────────────
    // In VEHICLE_PACKING mode, proactively try station-assisted insertion
    // even when direct insertion was found, because on R-type instances
    // energy is the real bottleneck — adding a station can unlock placing
    // customers in routes that would otherwise be rejected by energy checks.
    //
    // In other modes: only try if no direct insertion found (fallback).
    bool tryStationAssisted =
        (mode == Mode::VEHICLE_PACKING) || (bestInsertion.routeIdx == -1);

    if (tryStationAssisted) {
      int nearestStat = instance->getNearestStationId(customerId);
      if (nearestStat != -1) {
        for (size_t ri = 0; ri < routes.size(); ++ri) {
          auto &route = routes[ri];
          if (!route.quickCapacityCheck(custDemand))
            continue;

          const auto &nodes = route.getNodes();
          // Limit positions to check for performance (station-assisted is
          // O(n²))
          for (size_t i = 1; i < nodes.size(); ++i) {
            // Try: station BEFORE customer
            {
              Route copy = route;
              copy.addNode(customerId, i);
              copy.addNode(nearestStat, i); // station shifts customer to i+1
              copy.evaluate();
              if (copy.isFeasible()) {
                double dDist =
                    copy.getTotalDistance() - route.getTotalDistance();
                double dTime = copy.getTotalTime() - route.getTotalTime();
                // For packing mode: bonus for HIGH time utilization routes
                double packingBonus = 0.0;
                if (mode == Mode::VEHICLE_PACKING) {
                  double currentTimeUtil = route.getTotalTime() / timeHorizon;
                  // Routes already using 70%+ of time horizon = "full" routes
                  // → big bonus for stuffing more into them
                  packingBonus = currentTimeUtil * avgRouteDistance * 0.6;
                }
                double cost = dDist - packingBonus;
                if (cost < bestInsertion.cost) {
                  bestInsertion = {static_cast<int>(ri),
                                   static_cast<int>(i),
                                   cost,
                                   true,
                                   nearestStat,
                                   true};
                }
              }
            }
            // Try: station AFTER customer
            {
              Route copy2 = route;
              copy2.addNode(customerId, i);
              copy2.addNode(nearestStat, i + 1);
              copy2.evaluate();
              if (copy2.isFeasible()) {
                double dDist =
                    copy2.getTotalDistance() - route.getTotalDistance();
                double packingBonus = 0.0;
                if (mode == Mode::VEHICLE_PACKING) {
                  double currentTimeUtil = route.getTotalTime() / timeHorizon;
                  packingBonus = currentTimeUtil * avgRouteDistance * 0.6;
                }
                double cost = dDist - packingBonus;
                if (cost < bestInsertion.cost) {
                  bestInsertion = {static_cast<int>(ri),
                                   static_cast<int>(i),
                                   cost,
                                   true,
                                   nearestStat,
                                   false};
                }
              }
            }
          }
        }
      }
    }

    // ── Apply insertion ──────────────────────────────────────────────
    if (bestInsertion.routeIdx != -1) {
      auto &route = routes[bestInsertion.routeIdx];
      double oldTime = route.getTotalTime();
      double oldDist = route.getTotalDistance();

      if (bestInsertion.usedStation) {
        route.addNode(customerId, bestInsertion.position);
        if (bestInsertion.stationBefore) {
          route.addNode(bestInsertion.stationId, bestInsertion.position);
        } else {
          route.addNode(bestInsertion.stationId, bestInsertion.position + 1);
        }
      } else {
        route.addNode(customerId, bestInsertion.position);
      }
      route.evaluate();

      double newTime = route.getTotalTime();
      double newDist = route.getTotalDistance();
      totalDuration += (newTime - oldTime);
      totalRouteDistance += (newDist - oldDist);
      if (newTime > maxRouteDuration) {
        maxRouteDuration = newTime;
      }

    } else {
      // ── Fallback: Create new route ──────────────────────────────
      int newRouteId = solution.getNumRoutes();
      auto vehicle = std::make_shared<Vehicle>(
          newRouteId, instance->getVehicleCapacity(),
          instance->getVehicleBattery(), instance->getVehicleEnergyRate());
      Route newRoute(newRouteId, vehicle, instance);
      newRoute.addNode(customerId, 1);
      newRoute.evaluate();

      if (newRoute.isFeasible()) {
        solution.addRoute(newRoute);
        totalDuration += newRoute.getTotalTime();
        totalRouteDistance += newRoute.getTotalDistance();
        activeRoutes++;
      } else {
        // Customer alone is infeasible → try with nearest station
        int nearestStat = instance->getNearestStationId(customerId);
        if (nearestStat != -1) {
          Route withStat(newRouteId, vehicle, instance);
          withStat.addNode(nearestStat, 1);
          withStat.addNode(customerId, 2);
          withStat.evaluate();
          if (withStat.isFeasible()) {
            solution.addRoute(withStat);
            totalDuration += withStat.getTotalTime();
            totalRouteDistance += withStat.getTotalDistance();
            activeRoutes++;
          }
        }
      }
    }
  }
}

double AdaptiveInsertion::calculateCost(const InsertionResult &result,
                                        Mode mode, const Route &route,
                                        double meanRouteDuration,
                                        double maxRouteDuration, int customerId,
                                        int position, double avgRouteDistance,
                                        double timeHorizon) const {
  switch (mode) {
  case Mode::DISTANCE_FOCUSED: {
    return result.deltaDistance;
  }
  case Mode::VEHICLE_PACKING: {
    // ⭐ KEY FIX: Use TIME utilization, not capacity fill ratio.
    // On R-type instances, energy & time windows are the bottleneck,
    // not load capacity. We want to pack customers into routes that
    // are already "busy" (high time utilization), so that sparse routes
    // can potentially be eliminated.
    //
    // Also consider energy slack: prefer routes with higher slack
    // (more energy budget remaining = easier to absorb new customer).
    double currentTimeUtil = route.getTotalTime() / std::max(1.0, timeHorizon);

    // Energy slack: average slack across route nodes
    auto slacks = route.getEnergySlack();
    double avgSlack = 0.0;
    if (!slacks.empty()) {
      double total = 0.0;
      for (double s : slacks)
        total += std::max(0.0, s);
      avgSlack = total / slacks.size();
    }
    double battCap = instance->getVehicleBattery();
    double slackRatio = (battCap > 1e-9) ? (avgSlack / battCap) : 0.0;

    // Cost = deltaDistance
    //      - timeUtilization bonus (pack into busy routes)
    //      - energy slack bonus (prefer routes with energy headroom)
    return result.deltaDistance -
           currentTimeUtil * avgRouteDistance * 0.5 // time packing
           - slackRatio * avgRouteDistance * 0.3;   // energy headroom
  }
  case Mode::WORKLOAD_FOCUSED: {
    double newRouteTime = route.getTotalTime() + result.deltaTime;
    return newRouteTime + result.deltaDistance * 0.1;
  }
  case Mode::MAXTIME_FOCUSED: {
    double newRouteTime = route.getTotalTime() + result.deltaTime;
    double makespanIncrease = std::max(0.0, newRouteTime - maxRouteDuration);

    // Time-slack awareness: Favor insertion into positions with high time slack
    double timeSlackBonus = 0.0;
    auto timeSlacks = route.getTimeSlack();
    if (position >= 0 && position < (int)timeSlacks.size()) {
      timeSlackBonus = timeSlacks[position] * 0.15; // 15% slack bonus
    }

    // Extra bonus if customer has very tight time window
    auto custNode = instance->getNodeById(customerId);
    double twWidth = custNode->getDueDate() - custNode->getReadyTime();
    double tightnessPenalty = (twWidth < 30.0) ? (30.0 - twWidth) * 2.0 : 0.0;

    return makespanIncrease * 100.0 + result.deltaDistance - timeSlackBonus -
           tightnessPenalty;
  }
  default:
    return result.deltaDistance;
  }
}

} // namespace alns
