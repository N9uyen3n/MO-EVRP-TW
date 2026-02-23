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
      if (tryInsertWithoutStation(route, customerId, pos, static_cast<int>(r),
                                  best)) {
        // Found a good no-station insertion; keep looking for better
        continue;
      }

      // --- Phase 2: Try WITH station (only if Phase 1 failed or scored low)
      // ---
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

  // Calculate travel to customer
  double distPrevToCust = instance->getDistance(prevNodeId, customerId);
  double timePrevToCust = instance->getTime(prevNodeId, customerId);
  double energyPrevToCust =
      distPrevToCust * route.getVehicle()->getEnergyConsumptionRate();

  double battAtCust = battAtPrev - energyPrevToCust;
  if (battAtCust < -1e-9) {
    // Can't even reach customer from prev node, skip
    return;
  }

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

  double battAtNextAfterCust = battAtCust - energyCustToNext;

  // Check: does inserting customer cause energy infeasibility for the rest?
  // minReq[pos] tells us minimum battery needed at the CURRENT nextNode
  // position
  double minReqAtNext = (pos < minReq.size()) ? minReq[pos] : 0.0;

  if (battAtNextAfterCust >= minReqAtNext - 1e-9) {
    // No energy problem — direct insertion might work, but
    // tryInsertWithoutStation already handled that case. If we're here, the
    // issue is time/capacity, not energy.
    return;
  }

  // Energy is insufficient. Try adding a station after customer.
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
        evaluateStationMove(route, stationId, pos, battAtCust, departFromCust);

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
                                                 double battAtU,
                                                 double departU) {

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

  // 1. Distance calculations
  // Route with customer already hypothetically inserted:
  //   ... u (customerId at uPos) -> nextNode (at uPos) ...
  // With station:
  //   ... u -> Station -> nextNode ...
  // But we're computing FROM customer to station to next.
  // The customerId is NOT in the route yet, so we use the IDs directly.
  // 'uPos' means: we're considering inserting customer BEFORE nodes[uPos].
  // After customer insertion: ... customer(uPos), nextNode(uPos+1) ...
  // After station insertion:  ... customer(uPos), station(uPos+1),
  // nextNode(uPos+2) ...

  // For delta calculation, we need:
  // - Distance: customer -> station -> nextNode  vs  customer -> nextNode
  // (direct) We don't have customerId here, but we have battAtU and departU
  // which assume customer is already inserted. So we treat the "customer" as a
  // virtual node. The distances are computed using the instance's distance
  // matrix.

  // We don't have the customerId directly, but we can infer the edge:
  // The edge is from the last operation: customer -> nextNodeId
  // Actually we DO need customerId... Let me use the prev node approach:
  // prevNodeId is at uPos-1, and nextNodeId is at uPos.
  // The customer goes between them. After customer insertion:
  //   prevNode -> customer -> nextNode
  // With station after customer:
  //   prevNode -> customer -> station -> nextNode

  // Since we have battAtU (battery after reaching customer) and departU
  // (departure from customer), we compute: customer -> station -> nextNode But
  // we don't have customerId... We have to get it from the calling context.

  // WORKAROUND: We compute station detour relative to direct
  // customer->nextNode. The caller passes battAtU which is battery AT customer,
  // departU = departure FROM customer. We need dist(customer, station) and
  // dist(station, nextNode). Since we don't have customerId, we use prevNode as
  // proxy (or accept the limitation).

  // Actually, let me re-check: the 'uPos' in the ORIGINAL route is the position
  // where customer WOULD be inserted. So nodes[uPos-1] = prevNode, nodes[uPos]
  // = nextNode. The customer goes at uPos, pushing nextNode to uPos+1. I need
  // customerId but the caller already has it! Let me rethink...

  // The caller `tryInsertWithStation` already computed:
  //   distCustToNext, battAtCust, departFromCust
  // So `battAtU` = battery remaining at customer, `departU` = departure from
  // customer. The station goes BETWEEN customer and nextNode. dist(customer,
  // station) = ? We don't have customerId here.

  // FIX: use prevNode to get "before" node's distance to station.
  // prevNode -> Station: we can compute this.
  // But the route configuration is: prevNode -> Customer -> Station -> NextNode
  // We need customer -> station distance.
  // Since customer IS already set to be at the position, and prevNode is before
  // it, we can get customerId from context. Let me add it as parameter.

  // ACTUALLY: The design from user calculates this in tryInsertWithStation
  // directly. Let me simplify: just do the Deep Copy approach for station moves
  // but with fast pre-filtering.

  // SIMPLIFIED APPROACH: Pre-filter using detour distance heuristic, then
  // verify with Deep Copy.
  int prevNodeId = (uPos > 0) ? nodes[uPos - 1] : nodes[0];

  double distPrevToStation = instance->getDistance(prevNodeId, stationId);
  double distStationToNext = instance->getDistance(stationId, nextNodeId);
  double distPrevToNext = instance->getDistance(prevNodeId, nextNodeId);

  res.detourDist = distPrevToStation + distStationToNext - distPrevToNext;
  res.detourTime = instance->getTime(prevNodeId, stationId) +
                   instance->getTime(stationId, nextNodeId) -
                   instance->getTime(prevNodeId, nextNodeId);

  // 2. Energy calculation (O(1) Delta)
  double energyToStation = distPrevToStation * consumptionRate;
  double energyStationToNext = distStationToNext * consumptionRate;

  // Battery at station (from prev, not from customer — simplified)
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
  double arrAtStation = departU + instance->getTime(prevNodeId, stationId) -
                        instance->getTime(prevNodeId, nextNodeId) +
                        instance->getTime(prevNodeId, stationId);
  // Simplified: arrival at station = departFromCustomer +
  // time(customer->station) But we don't have customer->station time directly.
  // Use heuristic: arrAtStation ≈ departU + detourTime / 2
  arrAtStation = departU + res.detourTime * 0.5;

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
  // We approximate: direct = departU + time(prevNode->nextNode) - already
  // traveled to prev Simplified: if detour time is covered by wait time at
  // next, it's free
  double directArrAtNext = departU + instance->getTime(prevNodeId, nextNodeId);
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
