#include "../../../../include/alns/operators/repair/SmartStationRepair.h"
#include <algorithm>
#include <iostream>

namespace alns {

SmartStationRepair::SmartStationRepair(std::shared_ptr<Instance> instance)
    : instance(instance) {}

std::string SmartStationRepair::getName() const {
  return "Smart Station Repair";
}

void SmartStationRepair::execute(Solution &solution,
                                 const std::vector<int> &unservedCustomers,
                                 std::mt19937 &rng) {
  std::vector<int> customers = unservedCustomers;
  // Sort by TW tightness ascending (tightest TW first).
  // Rationale: customers with narrow TW have fewest feasible insertion
  // positions — insert them first before routes fill up.
  // Previously used shuffle() which destroyed all ordering information.
  std::sort(customers.begin(), customers.end(), [&](int a, int b) {
    auto na = instance->getNodeById(a);
    auto nb = instance->getNodeById(b);
    double twA = na->getDueDate() - na->getReadyTime();
    double twB = nb->getDueDate() - nb->getReadyTime();
    if (std::abs(twA - twB) < 1e-6)
      return na->getReadyTime() < nb->getReadyTime(); // tiebreak: earlier start
    return twA < twB;
  });

  auto &routes = solution.getRoutes();

  for (int customerId : customers) {
    InsertionCandidate bestInsertion;

    // 1. Try to insert into existing routes
    for (size_t r = 0; r < routes.size(); ++r) {
      auto &route = routes[r];
      const auto &nodes = route.getNodes();

      double custDemand = instance->getNodeById(customerId)->getDemand();
      // ⭐ Early capacity check — nếu route đã full thì cả Strategy A và B đều fail
      if (!route.quickCapacityCheck(custDemand)) continue;

      for (size_t i = 1; i < nodes.size();
           ++i) { // Insert at position i (after node i-1)
        // --- Strategy A: Direct Insertion ---
        auto result = route.checkInsertionCost(customerId, i);

        if (result.isFeasible) {
          double adjustedCost = result.deltaDistance;
          auto slack = route.getEnergySlack();
          if (i < slack.size()) {
            double slackBonus = slack[i] * 0.01;
            adjustedCost -= slackBonus;
          }
          if (adjustedCost < bestInsertion.costIncrease) {
            bestInsertion.customerId   = customerId;
            bestInsertion.routeIndex   = static_cast<int>(r);
            bestInsertion.position     = static_cast<int>(i);
            bestInsertion.costIncrease = adjustedCost;
            bestInsertion.requiresStation = false;
          }
        }

        // --- Strategy B: Station-Assisted Insertion ---
          // FIX #15: Strategy B luon chay de co the canh tranh voi Strategy A.
          // FIX #4: Dung do-while(false)+break thay vi goto de tranh UB khi
          //         nhay qua khai bao bien co constructor.
          // FIX #5: Dung states[i-1].departureTime (thuc te) thay vi readyTime+serviceTime
          //         (qua lac quan - xe co the den prevNode muon hon readyTime rat nhieu).
          do {
          int prevNodeId = nodes[i - 1];
          int nextNodeId = nodes[i];
          auto prevNode  = instance->getNodeById(prevNodeId);
          auto custNode  = instance->getNodeById(customerId);

          // FIX #5: lay departure time thuc te tu route states
          // Route da duoc evaluate truoc do (checkInsertionCost goi evaluate ngam)
          const auto& routeStates = route.getStates();
          double latestDepartPrev;
          if (!routeStates.empty() && (i - 1) < routeStates.size()) {
            latestDepartPrev = routeStates[i - 1].departureTime;
          } else {
            // Fallback: dung readyTime neu states chua co
            latestDepartPrev = prevNode->getReadyTime() + prevNode->getServiceTime();
          }
          double earliestArrivalCust = latestDepartPrev + instance->getTime(prevNodeId, customerId);
          if (earliestArrivalCust > custNode->getDueDate() + 1e-6) {
            // Ngay ca khi xe roi prevNode ngay sau khi phuc vu, van khong kip cust TW
            // -> station cung khong giai quyet duoc gap TW nay
            break; // FIX #4: dung break thay vi goto
          }

          // Option B1: Insert Station BEFORE Customer (Prev -> Station ->
          // Customer -> Next)
          struct SC {
            int id;
            double det;
          };
          const auto &stations = instance->getStations();

          std::vector<SC> candidatesB1;
          for (const auto &st : stations) {
            int sid = st->getId();
            double det = instance->getDistance(prevNodeId, sid) +
                         instance->getDistance(sid, customerId);
            candidatesB1.push_back({sid, det});
          }
          // FIX #11: Top-K = 5
          int topKB1 = std::min(5, (int)candidatesB1.size());
          std::partial_sort(candidatesB1.begin(), candidatesB1.begin() + topKB1,
                            candidatesB1.end(), [](const SC &a, const SC &b) {
                              return a.det < b.det;
                            });

          for (int k = 0; k < topKB1; ++k) {
            int stationId = candidatesB1[k].id;
            if (prevNodeId != stationId) {
              double costBuf = evaluateInsertionWithStation(route, customerId,
                                                            i, stationId, i);
              if (costBuf < bestInsertion.costIncrease) {
                bestInsertion.customerId = customerId;
                bestInsertion.routeIndex = static_cast<int>(r);
                bestInsertion.position = static_cast<int>(i);
                bestInsertion.costIncrease = costBuf;
                bestInsertion.requiresStation = true;
                bestInsertion.stationId = stationId;
                bestInsertion.stationPosition = static_cast<int>(i);
              }
            }
          }

          // Option B2: Insert Station AFTER Customer (Prev -> Customer ->
          // Station -> Next)
          std::vector<SC> candidatesB2;
          for (const auto &st : stations) {
            int sid = st->getId();
            double det = instance->getDistance(customerId, sid) +
                         instance->getDistance(sid, nextNodeId);
            candidatesB2.push_back({sid, det});
          }
          // FIX #11: Top-K = 5
          int topKB2 = std::min(5, (int)candidatesB2.size());
          std::partial_sort(candidatesB2.begin(), candidatesB2.begin() + topKB2,
                            candidatesB2.end(), [](const SC &a, const SC &b) {
                              return a.det < b.det;
                            });

          for (int k = 0; k < topKB2; ++k) {
            int stationId = candidatesB2[k].id;
            if (nextNodeId != stationId) {
              double costBuf = evaluateInsertionWithStation(
                  route, customerId, i, stationId, i + 1);
              if (costBuf < bestInsertion.costIncrease) {
                bestInsertion.customerId = customerId;
                bestInsertion.routeIndex = static_cast<int>(r);
                bestInsertion.position = static_cast<int>(i);
                bestInsertion.costIncrease = costBuf;
                bestInsertion.requiresStation = true;
                bestInsertion.stationId = stationId;
                bestInsertion.stationPosition = static_cast<int>(i + 1);
              }
            }
          }
          } while(false); // end Strategy B block
      }
    }

    // 2. Perform the best insertion found
    if (bestInsertion.routeIndex != -1) {
      auto &route = routes[bestInsertion.routeIndex];

      if (bestInsertion.requiresStation) {
        // Determine order based on station position
        if (bestInsertion.stationPosition == bestInsertion.position) {
          // Station BEFORE Customer
          // Insert Customer first at pos, then Station at pos (pushing
          // customer to pos+1) Wait: logic: Original: A (i-1), B (i) Goal: A,
          // Station, Customer, B
          // 1. Insert Customer at i: A, Customer, B
          // 2. Insert Station at i: A, Station, Customer, B
          route.addNode(bestInsertion.customerId, bestInsertion.position);
          route.addNode(bestInsertion.stationId, bestInsertion.position);
        } else {
          // Station AFTER Customer
          // Goal: A, Customer, Station, B
          // 1. Insert Customer at i: A, Customer, B
          // 2. Insert Station at i+1: A, Customer, Station, B
          route.addNode(bestInsertion.customerId, bestInsertion.position);
          route.addNode(bestInsertion.stationId, bestInsertion.position + 1);
        }
      } else {
        route.addNode(customerId, bestInsertion.position);
      }
      route.evaluate();
    } else {
      // Phase 5: Exhaustive station search — try ALL stations (capped at 10)
      // across ALL existing routes at ALL positions before creating new route.
      bool stationInserted = false;
      constexpr int MAX_STATIONS_TO_TRY = 10;

      const auto& allStations = instance->getStations();
      int stationsToTry = std::min(MAX_STATIONS_TO_TRY, (int)allStations.size());

      for (int si = 0; si < stationsToTry && !stationInserted; ++si) {
        int stationId = allStations[si]->getId();
        for (int ri = 0; ri < (int)routes.size() && !stationInserted; ++ri) {
          double custDemand = instance->getNodeById(customerId)->getDemand();
          if (!routes[ri].quickCapacityCheck(custDemand)) continue;

          const auto& rnodes = routes[ri].getNodes();
          for (int pos = 1; pos < (int)rnodes.size() && !stationInserted; ++pos) {
            // Try [station, customer] pattern
            for (bool stFirst : {true, false}) {
              Route testRoute = routes[ri];
              if (stFirst) {
                testRoute.addNode(customerId, pos);
                testRoute.addNode(stationId, pos);
              } else {
                testRoute.addNode(customerId, pos);
                testRoute.addNode(stationId, pos + 1);
              }
              testRoute.evaluate();
              if (testRoute.isFeasible()) {
                routes[ri] = testRoute;
                stationInserted = true;
                break;
              }
            }
          }
        }
      }

      if (!stationInserted) {
        // Only NOW create a new route
        int newRouteId = solution.getNumRoutes();
        auto vehicle = std::make_shared<Vehicle>(
            newRouteId, instance->getVehicleCapacity(),
            instance->getVehicleBattery(), instance->getVehicleEnergyRate());
        Route newRoute(newRouteId, vehicle, instance);
        newRoute.addNode(customerId, 1);
        newRoute.evaluate();

        if (newRoute.isFeasible()) {
          solution.addRoute(newRoute);
        } else {
          struct StDet { int id; double det; };
          std::vector<StDet> sortedStations;
          for (const auto& st : allStations) {
            int sid = st->getId();
            double det = std::min(
                instance->getDistance(0, sid) + instance->getDistance(sid, customerId),
                instance->getDistance(0, customerId) + instance->getDistance(customerId, sid));
            sortedStations.push_back({sid, det});
          }
          std::sort(sortedStations.begin(), sortedStations.end(),
                    [](const StDet& a, const StDet& b){ return a.det < b.det; });

          for (const auto& [nearestStat, det] : sortedStations) {
            Route withStat(newRouteId, vehicle, instance);
            withStat.addNode(nearestStat, 1);
            withStat.addNode(customerId, 2);
            withStat.evaluate();
            if (withStat.isFeasible()) {
              solution.addRoute(withStat);
              break;
            }
            Route withStatAfter(newRouteId, vehicle, instance);
            withStatAfter.addNode(customerId, 1);
            withStatAfter.addNode(nearestStat, 2);
            withStatAfter.evaluate();
            if (withStatAfter.isFeasible()) {
              solution.addRoute(withStatAfter);
              break;
            }
          }
        }
      }
    }
  }
}

// Helper to evaluate creating a temporary route with station
double SmartStationRepair::evaluateInsertionWithStation(const Route &route,
                                                        int customerId,
                                                        int index,
                                                        int stationId,
                                                        int stationIndex) {
  // Create a lightweight copy (if possible) or just standard copy
  // Since we need to modify structure, copy is necessary.
  Route routeCopy = route;

  // Perform insertions
  // Logic: If stationIndex == index, means Station before Customer.
  // If stationIndex == index + 1, means Station after Customer.

  // Note: When inserting multiple nodes, indices shift!
  // If we want [Station, Customer] at `index`:
  // 1. Insert Customer at `index`. Now: ... Prev, Customer, Next ...
  // 2. Insert Station at `index`. Now: ... Prev, Station, Customer, Next ...

  if (stationIndex == index) {
    routeCopy.addNode(customerId, index);
    routeCopy.addNode(stationId, index);
  } else {
    // Customer at `index`, Station at `index + 1`
    routeCopy.addNode(customerId, index);
    routeCopy.addNode(stationId, index + 1);
  }

  routeCopy.evaluate();

  if (routeCopy.isFeasible()) {
    return routeCopy.getTotalDistance() - route.getTotalDistance();
  }

  return std::numeric_limits<double>::infinity();
}

} // namespace alns