#include "../../../../include/alns/operators/repair/SmartTimeAwareStationRepair.h"
#include <iostream>

namespace alns {

SmartTimeAwareStationRepair::SmartTimeAwareStationRepair(
    std::shared_ptr<Instance> instance)
    : instance(instance) {}

std::string SmartTimeAwareStationRepair::getName() const {
  return "TimeAware Station Repair";
}

void SmartTimeAwareStationRepair::execute(
    Solution &solution, const std::vector<int> &unservedCustomers,
    std::mt19937 &rng) {
  std::vector<int> customers = unservedCustomers;
  std::shuffle(customers.begin(), customers.end(), rng);

  auto &routes = solution.getRoutes();

  for (int customerId : customers) {
    InsertionCandidate bestInsertion = findBestInsertion(customerId, solution);

    if (bestInsertion.routeIndex != -1) {
      auto &route = routes[bestInsertion.routeIndex];

      if (bestInsertion.requiresStation) {
        if (bestInsertion.stationPosition > bestInsertion.position) {
          // Station AFTER Customer: insert customer first, then station
          // Goal: ... Prev, Customer, Station, Next ...
          route.addNode(bestInsertion.customerId, bestInsertion.position);
          route.addNode(bestInsertion.stationId, bestInsertion.position + 1);
        } else {
          // Station BEFORE Customer: insert customer, then station at same pos
          // Goal: ... Prev, Station, Customer, Next ...
          route.addNode(bestInsertion.customerId, bestInsertion.position);
          route.addNode(bestInsertion.stationId, bestInsertion.position);
        }
      } else {
        route.addNode(customerId, bestInsertion.position);
      }
      route.evaluate();
    } else {
      // Fallback: Create new route
      int newRouteId = solution.getNumRoutes();
      auto vehicle = std::make_shared<Vehicle>(
          newRouteId, instance->getVehicleCapacity(),
          instance->getVehicleBattery(), instance->getVehicleEnergyRate());
      Route newRoute(newRouteId, vehicle, instance);
      newRoute.addNode(customerId, 1);
      newRoute.evaluate();
      solution.addRoute(newRoute);
    }
  }
}

SmartTimeAwareStationRepair::InsertionCandidate
SmartTimeAwareStationRepair::findBestInsertion(int customerId,
                                               Solution &solution) {
  InsertionCandidate best;
  best.customerId = customerId;

  auto &routes = solution.getRoutes();

  for (size_t r = 0; r < routes.size(); ++r) {
    auto &route = routes[r];
    route.evaluate(); // Ensure states & minBatteryReq are up to date
    const auto &nodes = route.getNodes();

    for (size_t pos = 1; pos < nodes.size(); ++pos) {
      // --- Phase 1: Try WITHOUT station ---
      tryInsertWithoutStation(route, customerId, pos, static_cast<int>(r), best);

      // --- Phase 2: Try WITH station ---
      // Always also try station-assisted insertion, as it may find better
      // insertion points (e.g., "Free Charge" opportunities) even if Phase 1 succeeded.
      tryInsertWithStation(route, customerId, pos, static_cast<int>(r), best);
    }
  }

  return best;
}

bool SmartTimeAwareStationRepair::tryInsertWithoutStation(
    const Route &route, int customerId, size_t pos, int routeIndex,
    InsertionCandidate &best) {

  // Use existing fast checks first
  if (!route.canPossiblyInsert(customerId, pos)) {
    return false;
  }

  // Full evaluation via checkInsertionCost (creates internal copy + evaluate)
  InsertionResult result = route.checkInsertionCost(customerId, pos);

  if (result.isFeasible && result.deltaDistance < best.costIncrease) {
    best.customerId = customerId;
    best.routeIndex = routeIndex;
    best.position = static_cast<int>(pos);
    best.costIncrease = result.deltaDistance;
    best.requiresStation = false;
    best.stationId = -1;
    best.stationPosition = -1;
    return true;
  }
  return false;
}

void SmartTimeAwareStationRepair::tryInsertWithStation(
    const Route &route, int customerId, size_t pos, int routeIndex,
    InsertionCandidate &best) {

  // We need the route to be evaluated to access states & minBatteryReq
  const auto &states = route.getStates();
  const auto &nodes = route.getNodes();
  const auto &minReq = route.getMinBatteryReq();

  if (states.empty() || pos >= nodes.size())
    return;

  // Simulate: what if we inserted customer at 'pos'?
  // We need departure time and battery at the node BEFORE insertion point
  int prevNodeId = nodes[pos - 1];
  int nextNodeId = nodes[pos];

  double departFromPrev = states[pos - 1].departureTime;
  double battAtPrev = states[pos - 1].remainingBattery;

  // Calculate travel to customer (Without station BEFORE)
  double distPrevToCust = instance->getDistance(prevNodeId, customerId);
  double timePrevToCust = instance->getTime(prevNodeId, customerId);
  double energyPrevToCust =
      distPrevToCust * route.getVehicle()->getEnergyConsumptionRate();

  double battAtCustDirect = battAtPrev - energyPrevToCust;

  // If we can't reach the customer directly from prevNode, we MUST place a station BEFORE the customer.
  if (battAtCustDirect < -1e-9) {
      // --- Phase 2a: Station BEFORE Customer ---
      auto prevNodeBase = instance->getNodeById(prevNodeId);
      auto custNodeBase = instance->getNodeById(customerId);
      double midX_before = (prevNodeBase->getX() + custNodeBase->getX()) / 2.0;
      double midY_before = (prevNodeBase->getY() + custNodeBase->getY()) / 2.0;

      std::vector<int> candidateStations = findKNearestStations(midX_before, midY_before, 3);
      for (int stationId : candidateStations) {
          if (stationId == prevNodeId) continue;
          auto stNode = instance->getNodeById(stationId);
          if (stNode->getType() != NodeType::STATION) continue;

          // Compute reachability to station
          double distPrevToStation = instance->getDistance(prevNodeId, stationId);
          double energyPrevToStation = distPrevToStation * route.getVehicle()->getEnergyConsumptionRate();
          double battAtStation = battAtPrev - energyPrevToStation;

          // Deep-copy verify: the only correct way to ensure feasibility
          // for the Station BEFORE Customer case
          Route testRoute = route;
          testRoute.addNode(customerId, pos);   // customer first
          testRoute.addNode(stationId, pos);    // station pushes customer to pos+1
          testRoute.evaluate();
          if (!testRoute.isFeasible()) continue;

          double actualCost = testRoute.getTotalDistance() - route.getTotalDistance();
          if (actualCost < best.costIncrease) {
              best.customerId = customerId;
              best.routeIndex = routeIndex;
              best.position = static_cast<int>(pos);
              best.costIncrease = actualCost;
              best.requiresStation = true;
              best.stationId = stationId;
              best.stationPosition = static_cast<int>(pos); // Station BEFORE customer
          }
      }
      // Cannot reach customer directly. Do NOT fall through to Phase 2b.
      return;
  }

  // --- Proceed with direct reachability logic (battAtCustDirect >= 0) ---
  auto custNode =
      std::dynamic_pointer_cast<Customer>(instance->getNodeById(customerId));
  if (!custNode)
    return;

  double arrAtCust = departFromPrev + timePrevToCust;
  double waitAtCust = std::max(0.0, custNode->getReadyTime() - arrAtCust);
  double departFromCust = std::max(arrAtCust, custNode->getReadyTime()) +
                          custNode->getServiceTime();

  // Calculate travel from customer to nextNode
  double distCustToNext = instance->getDistance(customerId, nextNodeId);
  double energyCustToNext =
      distCustToNext * route.getVehicle()->getEnergyConsumptionRate();

  double battAtNextAfterCust = battAtCustDirect - energyCustToNext;

  // Check: does inserting customer cause energy infeasibility for the rest?
  // minReq[pos] tells us minimum battery needed at the CURRENT nextNode
  // position
  double minReqAtNext = (pos < minReq.size()) ? minReq[pos] : 0.0;

  if (battAtNextAfterCust >= minReqAtNext - 1e-9) {
    // Energy is sufficient to reach next node without station.
    // Only try station if there is a "Free Charge" opportunity:
    // i.e., we would arrive at nextNode before its time window opens anyway.
    double directArrAtNext = departFromCust + instance->getTime(customerId, nextNodeId);
    auto nextNode = instance->getNodeById(nextNodeId);
    double slack = std::max(0.0, nextNode->getReadyTime() - directArrAtNext);
    if (slack < 1e-6) {
      return; // No energy problem AND no free charge opportunity. Skip.
    }
    // slack > 0 means we can absorb a station stop "for free" → fall through to Phase 2b.
  }

  // --- Phase 2b: Station AFTER Customer ---
  // Energy is insufficient to reach next node OR we want to find a Free Charge opportunity.
  // Find KNN stations near the midpoint of edge (customer -> nextNode)
  auto custNodeBase = instance->getNodeById(customerId);
  auto nextNodeBase = instance->getNodeById(nextNodeId);
  double midX = (custNodeBase->getX() + nextNodeBase->getX()) / 2.0;
  double midY = (custNodeBase->getY() + nextNodeBase->getY()) / 2.0;

  std::vector<int> candidateStations = findKNearestStations(midX, midY, 5);

  for (int stationId : candidateStations) {
    // Check: avoid redundant stations (same station already adjacent)
    if (stationId == prevNodeId || stationId == nextNodeId)
      continue;
    // Also check if prev or next is already a station at same location
    auto stNode = instance->getNodeById(stationId);
    if (stNode->getType() != NodeType::STATION)
      continue;

    StationMoveResult res =
        evaluateStationMove(route, stationId, pos, battAtCustDirect, departFromCust, customerId);

    if (res.isValid) {
      double score = calculateStationScore(res);
      // Convert score to cost (lower is better for InsertionCandidate)
      // Score is "efficiency" (higher better), so Cost = -Score + detour
      // penalty
      double costIncrease = res.detourDist;
      if (res.isFreeCharge) {
        costIncrease *= 0.01; // Heavily discount free charges
      }

      if (costIncrease < best.costIncrease) {
        best.customerId = customerId;
        best.routeIndex = routeIndex;
        best.position = static_cast<int>(pos);
        best.costIncrease = costIncrease;
        best.requiresStation = true;
        best.stationId = stationId;
        best.stationPosition =
            static_cast<int>(pos) + 1; // Station after customer
      }
    }
  }
}

SmartTimeAwareStationRepair::StationMoveResult
SmartTimeAwareStationRepair::evaluateStationMove(const Route &routeWithCust,
                                                 int stationId, size_t uPos,
                                                 double battAtU, double departU,
                                                 int customerId) {

  StationMoveResult res;
  res.stationId = stationId;

  const auto &nodes = routeWithCust.getNodes();
  const auto &minReq = routeWithCust.getMinBatteryReq();

  if (uPos >= nodes.size())
    return res;

  int nextNodeId =
      nodes[uPos]; // The node currently at 'uPos' (will be pushed to uPos+1)
  auto nextNode = instance->getNodeById(nextNodeId);
  auto stationNode =
      std::dynamic_pointer_cast<Station>(instance->getNodeById(stationId));
  if (!stationNode)
    return res;

  double consumptionRate =
      routeWithCust.getVehicle()->getEnergyConsumptionRate();
  double batteryCap = routeWithCust.getVehicle()->getBatteryCapacity();


  // Note: prevNodeId was formerly used for approximations but is no longer needed
  // after fixing to use customerId directly for all distance/time calculations.

  double distCustToStation = instance->getDistance(customerId, stationId);
  double distStationToNext = instance->getDistance(stationId, nextNodeId);
  double distCustToNext = instance->getDistance(customerId, nextNodeId);

  res.detourDist = distCustToStation + distStationToNext - distCustToNext;
  res.detourTime = instance->getTime(customerId, stationId) +
                   instance->getTime(stationId, nextNodeId) -
                   instance->getTime(customerId, nextNodeId);

  // 2. Energy calculation (O(1) Delta)
  double energyToStation = distCustToStation * consumptionRate;
  double energyStationToNext = distStationToNext * consumptionRate;

  // Battery at station (from customer)
  double battAtStation = battAtU - energyToStation;
  if (battAtStation < -1e-9)
    return res; // Can't reach station
  battAtStation = std::max(0.0, battAtStation);

  // Minimum required at nextNode
  double minReqAtNext = (uPos < minReq.size()) ? minReq[uPos] : 0.0;

  // Battery at next WITHOUT charging
  double battAtNextNoCharge = battAtStation - energyStationToNext;

  // Energy needed to charge
  res.energyNeeded = std::max(0.0, minReqAtNext - battAtNextNoCharge);

  // Can't charge more than battery capacity
  double maxCharge = batteryCap - battAtStation;
  if (res.energyNeeded > maxCharge + 1e-9)
    return res;

  // 3. Charging time
  double chargingRate = stationNode->getChargingRate();
  if (chargingRate <= 0)
    return res;
  res.totalChargeTime =
      res.energyNeeded * chargingRate; // chargingRate = minutes per unit energy

  // 4. Time window check at nextNode
  // Arrival at station correctly computed from customer departure
  double arrAtStation = departU + instance->getTime(customerId, stationId);

  // Check station time window
  if (arrAtStation > stationNode->getDueDate())
    return res;

  double departStation =
      std::max(arrAtStation, stationNode->getReadyTime()) + res.totalChargeTime;
  double arrAtNext = departStation + instance->getTime(stationId, nextNodeId);

  if (arrAtNext > nextNode->getDueDate())
    return res;

  // 5. Free Charge detection
  // Direct arrival at next (without station): departU + time(customer->next)
  // Check if detour time is covered by wait time at next
  double directArrAtNext = departU + instance->getTime(customerId, nextNodeId);
  double slackDirect =
      std::max(0.0, nextNode->getReadyTime() - directArrAtNext);

  if (res.detourTime + res.totalChargeTime <= slackDirect + 1e-9) {
    res.isFreeCharge = true;
  }

  res.isValid = true;
  return res;
}

double SmartTimeAwareStationRepair::calculateStationScore(
    const StationMoveResult &res) {
  double timeCost = res.detourTime + res.totalChargeTime + 0.001;

  const double W_ENERGY = 1000.0;

  // Efficiency: energy gained per time spent
  double efficiency = (res.energyNeeded * W_ENERGY) / timeCost;

  if (res.isFreeCharge) {
    efficiency *= 100.0;
  }

  return efficiency;
}

std::vector<int> SmartTimeAwareStationRepair::findKNearestStations(double midX,
                                                                   double midY,
                                                                   int k) {
  const auto &stations = instance->getStations();

  struct StationDist {
    int id;
    double dist;
    bool operator<(const StationDist &o) const { return dist < o.dist; }
  };

  std::vector<StationDist> candidates;
  candidates.reserve(stations.size());

  for (const auto &station : stations) {
    double dx = station->getX() - midX;
    double dy = station->getY() - midY;
    double dist = std::sqrt(dx * dx + dy * dy);
    candidates.push_back({station->getId(), dist});
  }

  int topK = std::min(k, static_cast<int>(candidates.size()));
  std::partial_sort(candidates.begin(), candidates.begin() + topK,
                    candidates.end());

  std::vector<int> result;
  result.reserve(topK);
  for (int i = 0; i < topK; ++i) {
    result.push_back(candidates[i].id);
  }

  return result;
}

} // namespace alns
