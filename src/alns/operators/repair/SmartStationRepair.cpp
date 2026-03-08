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
  std::shuffle(customers.begin(), customers.end(), rng);

  auto &routes = solution.getRoutes();

  for (int customerId : customers) {
    InsertionCandidate bestInsertion;

    // 1. Try to insert into existing routes
    for (size_t r = 0; r < routes.size(); ++r) {
      auto &route = routes[r];
      const auto &nodes = route.getNodes();

      for (size_t i = 1; i < nodes.size();
           ++i) { // Insert at position i (after node i-1)
        // --- Strategy A: Direct Insertion ---
        // Fast check using route's internal delta calculation
        auto result = route.checkInsertionCost(customerId, i);

        if (result.isFeasible) {
          // Energy Slack tiebreaker: prefer positions with higher slack
          double adjustedCost = result.deltaDistance;
          auto slack = route.getEnergySlack();
          if (i < slack.size()) {
            double slackBonus =
                slack[i] * 0.01; // Small bonus for high-slack positions
            adjustedCost -= slackBonus;
          }
          if (adjustedCost < bestInsertion.costIncrease) {
            bestInsertion.customerId = customerId;
            bestInsertion.routeIndex = static_cast<int>(r);
            bestInsertion.position = static_cast<int>(i);
            bestInsertion.costIncrease = adjustedCost;
            bestInsertion.requiresStation = false;
          }
        } else {
          // --- Strategy B: Station-Assisted Insertion ---
          // Only consider if infeasible due to ENERGY (not capacity or time)
          // Note: checkInsertionCost doesn't explicitly return failure reason,
          // but we can infer or just try anyway if it's promising.

          // Simple logic: If direct insertion fails, try adding a station.
          // To avoid explosion, only try if the customer is somewhat near the
          // route. (Here we skip the expensive check for now and just try).

          int prevNodeId = nodes[i - 1];
          int nextNodeId =
              nodes[i]; // The node that will be AFTER the new customer

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
          int topKB1 = std::min(3, (int)candidatesB1.size());
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
          int topKB2 = std::min(3, (int)candidatesB2.size());
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
        }
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
      // 3. Create new route if no insertion found
      int newRouteId = solution.getNumRoutes();
      auto vehicle = std::make_shared<Vehicle>(
          newRouteId, instance->getVehicleCapacity(),
          instance->getVehicleBattery(), instance->getVehicleEnergyRate());
      Route newRoute(newRouteId, vehicle, instance);
      newRoute.addNode(customerId, 1); // 0 is depot, so insert at 1
      newRoute.evaluate();
      solution.addRoute(newRoute);
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
