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

void AdaptiveInsertion::setVehicleReductionMode(bool active) {
  vehicleReductionMode_ = active;
}

void AdaptiveInsertion::execute(Solution &solution,
                                const std::vector<int> &unservedCustomers,
                                std::mt19937 &rng) {
  std::uniform_real_distribution<> randDist(0.0, 1.0);
  double r = randDist(rng);

  Mode mode;
  double distShare = weightHint_.dist;

  if (vehicleReductionMode_) {
    // VR mode: heavily bias toward VEHICLE_PACKING.
    // Goal is to pack customers into fewest routes possible,
    // not to minimize distance. PACKING 70%, DISTANCE 20%, others 10%.
    if (r < 0.70) {
      mode = Mode::VEHICLE_PACKING;
    } else if (r < 0.90) {
      mode = Mode::DISTANCE_FOCUSED;
    } else {
      mode = Mode::MAXTIME_FOCUSED;
    }
  } else {
    // Normal mode: original logic unchanged
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
      // FIX #1: Min-detour per position. FIX #4: Thu top-3 stations thay vi chi 1.
      // Station co min-detour nhat doi khi vi pham TW, station thu 2-3 co the OK.
      const auto& allStations = instance->getStations();
      if (!allStations.empty()) {
        for (size_t ri = 0; ri < routes.size(); ++ri) {
          auto &route = routes[ri];
          if (!route.quickCapacityCheck(custDemand))
            continue;

          const auto &nodes = route.getNodes();
          for (size_t i = 1; i < nodes.size(); ++i) {
            int prevId = nodes[i - 1];
            int nextId = nodes[i];

            // FIX #4: Build sorted top-K lists cho B1 va B2
            struct StDet { int id; double det; };
            const int topK = std::min(3, (int)allStations.size());

            // Top-K cho B1: station TRUOC customer
            std::vector<StDet> candsB1;
            for (const auto& st : allStations) {
              int sid = st->getId();
              candsB1.push_back({sid,
                instance->getDistance(prevId, sid) + instance->getDistance(sid, customerId)});
            }
            std::partial_sort(candsB1.begin(), candsB1.begin() + topK, candsB1.end(),
                              [](const StDet& a, const StDet& b){ return a.det < b.det; });

            for (int k = 0; k < topK; ++k) {
              int sid = candsB1[k].id;
              Route copy = route;
              copy.addNode(customerId, i);
              copy.addNode(sid, i);
              copy.evaluate();
              if (copy.isFeasible()) {
                InsertionResult synRes;
                synRes.isFeasible = true;
                synRes.deltaDistance  = copy.getTotalDistance()  - route.getTotalDistance();
                synRes.deltaTime      = copy.getTotalTime()       - route.getTotalTime();
                synRes.deltaEnergyConsumption = copy.getTotalEnergyConsumption()
                                              - route.getTotalEnergyConsumption();
                synRes.deltaChargeAmount = copy.getTotalChargeAmount()
                                         - route.getTotalChargeAmount();
                double cost = calculateCost(synRes, mode, route, meanRouteDuration,
                                            maxRouteDuration, customerId, (int)i,
                                            avgRouteDistance, timeHorizon);
                if (cost < bestInsertion.cost) {
                  bestInsertion = {static_cast<int>(ri), static_cast<int>(i),
                                   cost, true, sid, true};
                }
              }
            }

            // Top-K cho B2: station SAU customer
            std::vector<StDet> candsB2;
            for (const auto& st : allStations) {
              int sid = st->getId();
              candsB2.push_back({sid,
                instance->getDistance(customerId, sid) + instance->getDistance(sid, nextId)});
            }
            std::partial_sort(candsB2.begin(), candsB2.begin() + topK, candsB2.end(),
                              [](const StDet& a, const StDet& b){ return a.det < b.det; });

            for (int k = 0; k < topK; ++k) {
              int sid = candsB2[k].id;
              Route copy2 = route;
              copy2.addNode(customerId, i);
              copy2.addNode(sid, i + 1);
              copy2.evaluate();
              if (copy2.isFeasible()) {
                InsertionResult synRes2;
                synRes2.isFeasible = true;
                synRes2.deltaDistance  = copy2.getTotalDistance()  - route.getTotalDistance();
                synRes2.deltaTime      = copy2.getTotalTime()       - route.getTotalTime();
                synRes2.deltaEnergyConsumption = copy2.getTotalEnergyConsumption()
                                               - route.getTotalEnergyConsumption();
                synRes2.deltaChargeAmount = copy2.getTotalChargeAmount()
                                          - route.getTotalChargeAmount();
                double cost = calculateCost(synRes2, mode, route, meanRouteDuration,
                                            maxRouteDuration, customerId, (int)i,
                                            avgRouteDistance, timeHorizon);
                if (cost < bestInsertion.cost) {
                  bestInsertion = {static_cast<int>(ri), static_cast<int>(i),
                                   cost, true, sid, false};
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
      totalDuration      += (newTime - oldTime);
      totalRouteDistance += (newDist - oldDist);
      // ⭐ FIX: update avgRouteDistance sau mỗi insertion
      // Cũ: avgRouteDistance là snapshot trước vòng lặp → stale sau vài insertions
      // → packingBonus trong Phase 2 dùng giá trị cũ, không phản ánh state thực
      if (activeRoutes > 0)
        avgRouteDistance = totalRouteDistance / activeRoutes; // recalculate inline
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
        // FIX #8: totalRouteDistance phai duoc cap nhat cho tat ca cac nhanh,
        // ke ca solo route feasible. Truoc day chi cap nhat trong nhanh withStat.
        totalRouteDistance += newRoute.getTotalDistance();
        activeRoutes++;
      } else {
        // Customer alone is infeasible - try with nearest station
        int nearestStat = instance->getNearestStationId(customerId);
        if (nearestStat != -1) {
          // Pattern [station, customer]
          Route withStat(newRouteId, vehicle, instance);
          withStat.addNode(nearestStat, 1);
          withStat.addNode(customerId, 2);
          withStat.evaluate();
          if (withStat.isFeasible()) {
            solution.addRoute(withStat);
            totalDuration += withStat.getTotalTime();
            totalRouteDistance += withStat.getTotalDistance();
            activeRoutes++;
          } else {
            // FIX #7: Thu them pattern [customer, station]
            // Quan trong voi khach hang gan depot nhung xa tram sac ve phia depot
            Route withStatAfter(newRouteId, vehicle, instance);
            withStatAfter.addNode(customerId, 1);
            withStatAfter.addNode(nearestStat, 2);
            withStatAfter.evaluate();
            if (withStatAfter.isFeasible()) {
              solution.addRoute(withStatAfter);
              totalDuration += withStatAfter.getTotalTime();
              totalRouteDistance += withStatAfter.getTotalDistance();
              activeRoutes++;
            }
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
    // ⭐ FIX: scale mismatch cũ — newRouteTime (~200-300) vs deltaDistance*0.1 (~0.5-5)
    // → deltaDistance bị nuốt hoàn toàn, mode này chỉ optimize newRouteTime.
    // Fix: normalize cả hai về cùng scale [0,1] bằng timeHorizon và avgRouteDistance.
    double newRouteTime    = route.getTotalTime() + result.deltaTime;
    double normalizedTime  = newRouteTime / std::max(1.0, timeHorizon);
    double normalizedDist  = (avgRouteDistance > 1e-6)
                               ? result.deltaDistance / avgRouteDistance
                               : result.deltaDistance * 0.01;
    // 70% time balance, 30% distance — workload focus vẫn ưu tiên time
    return 0.7 * normalizedTime + 0.3 * normalizedDist;
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