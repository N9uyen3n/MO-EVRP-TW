#include "../../../../include/alns/operators/repair/SmartTimeAwareStationRepair.h"
#include <unordered_set>

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

  // Nâng Cấp 1: Sort khách hàng theo Insertion Difficulty thay vì Random
  // Khách hàng có TW chặt và ở xa depot sẽ khó chèn hơn -> Ưu tiên chèn trước
  std::sort(customers.begin(), customers.end(), [&](int a, int b) {
    auto ca = instance->getNodeById(a);
    auto cb = instance->getNodeById(b);

    double twA = ca->getDueDate() - ca->getReadyTime();
    double twB = cb->getDueDate() - cb->getReadyTime();

    double eA = instance->getDistance(0, a);
    double eB = instance->getDistance(0, b);

    // Score = 1/twWidth (càng nhỏ càng điểm cao) + Khoảng cách (càng xa càng
    // cực)
    double scoreA = (1.0 / std::max(1.0, twA)) + eA * 0.01;
    double scoreB = (1.0 / std::max(1.0, twB)) + eB * 0.01;

    return scoreA > scoreB; // Sort Descending (Khó nhất lên đầu)
  });
  auto &routes = solution.getRoutes();

  // Evaluate all routes once before starting the repair
  for (auto &route : routes) {
    route.evaluate();
  }

  for (int customerId : customers) {
    InsertionCandidate bestInsertion = findBestInsertion(customerId, solution);

    if (bestInsertion.routeIndex != -1) {
      auto &route = routes[bestInsertion.routeIndex];

      if (bestInsertion.requiresStation) {
        // ── Nâng Cấp 5: Deep-Copy Verification ──
        // Delta approximation trong Phase 2b đôi khi không phản ánh chính xác Time Ripple.
        // Cần deep-copy check trước khi commit.
        Route verifyRoute = routes[bestInsertion.routeIndex];
        if (bestInsertion.stationPosition > bestInsertion.position) {
          verifyRoute.addNode(bestInsertion.customerId, bestInsertion.position);
          verifyRoute.addNode(bestInsertion.stationId, bestInsertion.position + 1);
        } else {
          verifyRoute.addNode(bestInsertion.stationId, bestInsertion.position);
          verifyRoute.addNode(bestInsertion.customerId, bestInsertion.position + 1);
        }
        verifyRoute.evaluate();
        
        if (verifyRoute.isFeasible()) {
            routes[bestInsertion.routeIndex] = verifyRoute; // Commit
        } else {
            // Verification failed (due to unaccounted Time Ripple). Fallback to new route.
            int newRouteId = solution.getNumRoutes();
            auto vehicle = std::make_shared<Vehicle>(
                newRouteId, instance->getVehicleCapacity(),
                instance->getVehicleBattery(), instance->getVehicleEnergyRate());
            Route newRoute(newRouteId, vehicle, instance);
            newRoute.addNode(customerId, 1);
            newRoute.evaluate();
            solution.addRoute(newRoute);
        }
      } else {
        route.addNode(customerId, bestInsertion.position);
        route.evaluate();
      }
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
    const auto &route = routes[r];
    const auto &nodes = route.getNodes();

    for (size_t pos = 1; pos < nodes.size(); ++pos) {
      // --- Phase 1: Try WITHOUT station ---
      tryInsertWithoutStation(route, customerId, pos, static_cast<int>(r),
                              best);

      // --- Phase 2: Try WITH station ---
      // Always also try station-assisted insertion, as it may find better
      // insertion points (e.g., "Free Charge" opportunities) even if Phase 1
      // succeeded.
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

  if (result.isFeasible) {
    // ── Nâng Cấp 2: Time-Aware Cost Function ──
    // Tính Time Ripple (sóng lan thời gian) để đo mức độ phá vỡ cấu trúc TW
    double timeRipple = computeTimeRipple(route, customerId, pos);

    // Cost kết hợp Balance (Distance, Time, Ripple)
    double adjustedCost = result.deltaDistance * 0.4
                        + result.deltaTime * 0.3
                        + timeRipple * 0.3;

    // Energy Slack tiebreaker: favor positions with higher slack
    auto energySlack = route.getEnergySlack();
    if (pos < energySlack.size()) {
      adjustedCost -= energySlack[pos] * 0.01;
    }

    // Time Slack Bonus: Ưu tiên chèn vào nơi có khoảng hở thời gian lớn
    auto timeSlacks = route.getTimeSlack();
    if (pos < timeSlacks.size()) {
      adjustedCost -= timeSlacks[pos] * 0.05;
    }

    if (adjustedCost < best.costIncrease) {
      best.customerId = customerId;
      best.routeIndex = routeIndex;
      best.position = static_cast<int>(pos);
      best.costIncrease = adjustedCost;
      best.requiresStation = false;
      best.stationId = -1;
      best.stationPosition = -1;
      return true;
    }
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

  // If we can't reach the customer directly from prevNode, we MUST place a
  // station BEFORE the customer.
  if (battAtCustDirect < -1e-9) {
    // --- Phase 2a: Station BEFORE Customer ---
    auto prevNodeBase = instance->getNodeById(prevNodeId);
    auto custNodeBase = instance->getNodeById(customerId);
    double midX_before = (prevNodeBase->getX() + custNodeBase->getX()) / 2.0;
    double midY_before = (prevNodeBase->getY() + custNodeBase->getY()) / 2.0;

    std::vector<int> candidateStations =
        findKNearestStations(midX_before, midY_before, 3);
    for (int stationId : candidateStations) {
      if (stationId == prevNodeId)
        continue;
      auto stNode = instance->getNodeById(stationId);
      if (stNode->getType() != NodeType::STATION)
        continue;

      // Deep-copy verify: the only correct way to ensure feasibility
      // for the Station BEFORE Customer case
      Route testRoute = route;
      testRoute.addNode(stationId, pos);      // station at pos first
      testRoute.addNode(customerId, pos + 1); // customer after station
      testRoute.evaluate();
      if (!testRoute.isFeasible())
        continue;

      // FIX: Cost function must include TIME penalty
      // Cost = DeltaDistance + Weight * DeltaTime
      // Weight ~ 1.0 (treating 1 unit of time as roughly equal to 1 unit of
      // distance cost-wise) Or use a smaller weight if distance is dominant.
      double deltaDist =
          testRoute.getTotalDistance() - route.getTotalDistance();

      // We don't have easy access to delta time without full route comparison,
      // but since we evaluated testRoute, we can check maxTime or just use
      // distance plus a penalty for the stop. Better: Use the station charging
      // time as a proxy for time cost.

      // Let's use a simple weighted sum.
      // Assuming speed = 1, distance ~ time.
      // But charging takes extra time.

      double costMetric = deltaDist;

      // Add penalty for charging time (converted to distance equivalent)
      // Assuming 1 unit time ~ 1 unit distance (common in VRPTW benchmarks
      // where speed=1) We add the charging time to the cost. We can retrieve
      // the charging time from the route states if needed, but here we just
      // trust the feasibility and use distance + slight penalty.

      // Actually, for "r" and "rc", minimizing vehicles is key, which means
      // minimizing time waste. So we should penalize the station insertion
      // heavily if it takes long.

      // Let's try to estimate charge time from the testRoute states?
      // Too complex. Let's stick to distance but add a penalty for the station
      // stop itself.
      costMetric += 10.0; // Fixed penalty for adding a stop

      if (costMetric < best.costIncrease) {
        best.customerId = customerId;
        best.routeIndex = routeIndex;
        best.position = static_cast<int>(pos);
        best.costIncrease = costMetric;
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
  // double waitAtCust = std::max(0.0, custNode->getReadyTime() - arrAtCust);
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
    // Proactive Charging: Try station if there is a "Free Charge" opportunity
    // OR if the remaining battery after reaching the next node is dangerously
    // low.
    double directArrAtNext =
        departFromCust + instance->getTime(customerId, nextNodeId);
    auto nextNode = instance->getNodeById(nextNodeId);
    double slackTime =
        std::max(0.0, nextNode->getReadyTime() - directArrAtNext);

    double battCap = route.getVehicle()->getBatteryCapacity();
    double energyHeadroom = battAtNextAfterCust - minReqAtNext;
    bool isEnergyTight = (energyHeadroom < 0.20 * battCap);

    if (slackTime < 1e-6 && !isEnergyTight) {
      return; // No energy problem AND no free charge opportunity. Skip.
    }
    // Phase 2b will now execute for Free Charge OR Tight Energy scenarios.
  }

  // --- Phase 2b: Station AFTER Customer ---
  // Energy is insufficient to reach next node OR we want to find a Free Charge opportunity.
  // ── Nâng Cấp 4: Expanded Station Search ──
  // Tìm KNN stations ở 3 vùng: Gần Customer, Gần Next Node, và vùng Giữa
  auto custNodeBase = instance->getNodeById(customerId);
  auto nextNodeBase = instance->getNodeById(nextNodeId);
  double midX = (custNodeBase->getX() + nextNodeBase->getX()) / 2.0;
  double midY = (custNodeBase->getY() + nextNodeBase->getY()) / 2.0;

  std::vector<int> stNearCust = findKNearestStations(custNodeBase->getX(), custNodeBase->getY(), 3);
  std::vector<int> stNearMid = findKNearestStations(midX, midY, 3);
  std::vector<int> stNearNext = findKNearestStations(nextNodeBase->getX(), nextNodeBase->getY(), 3);

  std::vector<int> candidateStations;
  std::unordered_set<int> seenStations;
  auto addUnique = [&](const std::vector<int>& stations) {
      for (int s : stations) {
          if (seenStations.insert(s).second) {
              candidateStations.push_back(s);
          }
      }
  };
  addUnique(stNearCust);
  addUnique(stNearMid);
  addUnique(stNearNext);

  for (int stationId : candidateStations) {
    // Check: avoid redundant stations (same station already adjacent)
    if (stationId == prevNodeId || stationId == nextNodeId)
      continue;
    // Also check if prev or next is already a station at same location
    auto stNode = instance->getNodeById(stationId);
    if (stNode->getType() != NodeType::STATION)
      continue;

    StationMoveResult res = evaluateStationMove(
        route, stationId, pos, battAtCustDirect, departFromCust, customerId);

    if (res.isValid) {
      // FIX: Cost Function Update
      // Old: double costIncrease = res.detourDist;
      // New: Combine Distance and Time (Charge Time + Detour Time)

      double timePenaltyWeight =
          1.0; // Adjust based on instance characteristics (1.0 is standard)

      // Total Cost = Detour Distance + Weight * (Detour Time + Charge Time)
      double totalTimeCost = res.detourTime + res.totalChargeTime;
      double costIncrease =
          res.detourDist + (timePenaltyWeight * totalTimeCost);

      if (res.isFreeCharge) {
        // If it's a free charge (hidden in waiting time), reduce cost
        // significantly to encourage taking this opportunity.
        costIncrease *= 0.1;
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

  // Note: prevNodeId was formerly used for approximations but is no longer
  // needed after fixing to use customerId directly for all distance/time
  // calculations.

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
  // PARTIAL RECHARGE STRATEGY: Charge only what is needed to meet minReq at
  // next node
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
  // This function is currently unused in the decision logic above,
  // but kept for potential future use.
  double timeCost = res.detourTime + res.totalChargeTime + 0.001;
  const double W_ENERGY = 1000.0;
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

// ── Nâng Cấp 3: Khảo sát Time Ripple (Sóng Lan Thời Gian) ──
double SmartTimeAwareStationRepair::computeTimeRipple(const Route &route, int customerId, size_t pos) const {
    const auto &states = route.getStates();
    const auto &nodes = route.getNodes();
    if (pos >= nodes.size() || states.empty()) return 0.0;
    
    // Time delay caused by inserting customer at pos
    int prevId = nodes[pos-1];
    int nextId = nodes[pos];
    double oldDirectTime = instance->getTime(prevId, nextId);
    
    auto custNode = instance->getNodeById(customerId);
    double newTime = instance->getTime(prevId, customerId) 
                   + custNode->getServiceTime()
                   + instance->getTime(customerId, nextId);
                   
    double arrAtCust = states[pos-1].departureTime + instance->getTime(prevId, customerId);
    double waitAtCust = std::max(0.0, custNode->getReadyTime() - arrAtCust);
    
    double delay = newTime + waitAtCust - oldDirectTime;
    
    // Propagate delay through subsequent nodes
    double ripple = 0.0;
    double cumulativeDelay = delay;
    
    for (size_t i = pos; i < nodes.size() && cumulativeDelay > 1e-9; ++i) {
        auto node = instance->getNodeById(nodes[i]);
        double newArr = states[i].arrivalTime + cumulativeDelay;
        double slack = node->getDueDate() - newArr;
        
        if (slack < 0) {
            ripple += std::abs(slack) * 10.0; // TW violation = heavy penalty
        }
        
        // Absorb delay via natural waiting time
        double currentWait = std::max(0.0, node->getReadyTime() - states[i].arrivalTime);
        double newWait = std::max(0.0, node->getReadyTime() - newArr);
        
        cumulativeDelay -= (currentWait - newWait); // Reduction in wait time absorbs delay
        cumulativeDelay = std::max(0.0, cumulativeDelay);
    }
    
    return ripple;
}

} // namespace alns
