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

  // Fix #4: Normalize cả 2 terms trước khi combine để tránh scale mismatch.
  // 1/twA (đơn vị: 1/phút) và eA*0.01 (đơn vị: km) không cộng được trực tiếp.
  // Giải pháp: normalize bằng average của từng term qua tất cả customers.
  double sumTW = 0.0, sumDist = 0.0;
  for (int id : customers) {
    auto c = instance->getNodeById(id);
    sumTW += c->getDueDate() - c->getReadyTime();
    sumDist += instance->getDistance(0, id);
  }
  double n = std::max(1.0, static_cast<double>(customers.size()));
  double avgTW = std::max(1.0, sumTW / n);
  double avgDist = std::max(1.0, sumDist / n);

  std::sort(customers.begin(), customers.end(), [&](int a, int b) {
    auto ca = instance->getNodeById(a);
    auto cb = instance->getNodeById(b);

    double twA = ca->getDueDate() - ca->getReadyTime();
    double twB = cb->getDueDate() - cb->getReadyTime();

    double eA = instance->getDistance(0, a);
    double eB = instance->getDistance(0, b);

    // Normalize: cả 2 terms đều ∈ [0, ~1] sau normalization
    // TW term: càng nhỏ → càng khó (TW chặt) → score cao hơn
    // Dist term: càng xa → càng khó → score cao hơn
    double scoreA = (avgTW / std::max(1.0, twA)) + (eA / avgDist) * 0.3;
    double scoreB = (avgTW / std::max(1.0, twB)) + (eB / avgDist) * 0.3;

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
        // Delta approximation trong Phase 2b đôi khi không phản ánh chính xác
        // Time Ripple. Cần deep-copy check trước khi commit.
        Route verifyRoute = routes[bestInsertion.routeIndex];
        if (bestInsertion.stationPosition > bestInsertion.position) {
          verifyRoute.addNode(bestInsertion.customerId, bestInsertion.position);
          verifyRoute.addNode(bestInsertion.stationId,
                              bestInsertion.position + 1);
        } else {
          verifyRoute.addNode(bestInsertion.stationId, bestInsertion.position);
          verifyRoute.addNode(bestInsertion.customerId,
                              bestInsertion.position + 1);
        }
        verifyRoute.evaluate();

        if (verifyRoute.isFeasible()) {
          routes[bestInsertion.routeIndex] = verifyRoute; // Commit
        } else {
          // Fix #5: Verification failed — customer cần station nhưng
          // station-assisted insertion vào existing route không feasible.
          // Thử tạo solo route D→S→C→D (nearest station) trước.
          // Nếu vẫn fail, tạo route trần và chỉ add nếu feasible.
          int newRouteId = solution.getNumRoutes();
          auto vehicle = std::make_shared<Vehicle>(
              newRouteId, instance->getVehicleCapacity(),
              instance->getVehicleBattery(), instance->getVehicleEnergyRate());

          bool placed = false;

          // Thử D → S → C → D với tất cả stations (nearest first)
          const auto &allStations = instance->getStations();
          // Sort stations by distance to customer
          std::vector<std::pair<double, int>> stDistances;
          stDistances.reserve(allStations.size());
          for (const auto &st : allStations) {
            stDistances.push_back(
                {instance->getDistance(customerId, st->getId()), st->getId()});
          }
          std::sort(stDistances.begin(), stDistances.end());

          for (const auto &[dist, sid] : stDistances) {
            // Try: D → sid → C → D
            Route soloWithSt(newRouteId, vehicle, instance);
            soloWithSt.addNode(sid, 1);
            soloWithSt.addNode(customerId, 2);
            soloWithSt.evaluate();
            if (soloWithSt.isFeasible()) {
              solution.addRoute(soloWithSt);
              placed = true;
              break;
            }
            // Try: D → C → sid → D
            Route soloWithSt2(newRouteId, vehicle, instance);
            soloWithSt2.addNode(customerId, 1);
            soloWithSt2.addNode(sid, 2);
            soloWithSt2.evaluate();
            if (soloWithSt2.isFeasible()) {
              solution.addRoute(soloWithSt2);
              placed = true;
              break;
            }
          }

          if (!placed) {
            // Last resort: route trần — chỉ add nếu feasible
            Route soloRoute(newRouteId, vehicle, instance);
            soloRoute.addNode(customerId, 1);
            soloRoute.evaluate();
            if (soloRoute.isFeasible()) {
              solution.addRoute(soloRoute);
            }
            // Nếu infeasible: không add — customer bị unserved,
            // ALNS sẽ xử lý ở iteration tiếp theo
          }
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
    // ⭐ Fix cost metric: deltaTime từ checkInsertionCost = depot_arrival_delta
    // (thay đổi tổng thời gian route), không phải "delay" do insertion.
    // deltaDistance là metric chính xác và scale-consistent hơn.
    // deltaChargeAmount dùng làm secondary để penalize insertion cần sạc thêm.
    double adjustedCost = result.deltaDistance
                          + result.deltaChargeAmount * 0.5  // penalty nếu buộc sạc thêm
                          + std::max(0.0, result.deltaWaitTime) * 0.1; // penalty wait tăng

    // Energy Slack tiebreaker: favor positions with higher slack (ít rủi ro battery hơn)
    auto energySlack = route.getEnergySlack();
    if (pos < energySlack.size()) {
      adjustedCost -= energySlack[pos] * 0.005; // nhỏ hơn để không dominate primary cost
    }

    // Time Slack Bonus: chèn vào node có nhiều slack → ít ripple hơn
    auto timeSlacks = route.getTimeSlack();
    if (pos < timeSlacks.size()) {
      adjustedCost -= timeSlacks[pos] * 0.002;
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

    const double consumptionRate = route.getVehicle()->getEnergyConsumptionRate();
    const double battCap = route.getVehicle()->getBatteryCapacity();

    for (int stationId : candidateStations) {
      if (stationId == prevNodeId)
        continue;
      auto stNode = instance->getNodeById(stationId);
      if (stNode->getType() != NodeType::STATION)
        continue;

      // Fix #2: O(1) pre-filter trước deep copy — loại bỏ ~80% candidates
      // Check 1: battery từ prev đến station
      double distPrevToSt = instance->getDistance(prevNodeId, stationId);
      double battAtSt = battAtPrev - distPrevToSt * consumptionRate;
      if (battAtSt < -1e-9)
        continue; // Không đến được station

      // Check 2: TW của station
      double arrAtSt = departFromPrev + instance->getTime(prevNodeId, stationId);
      if (arrAtSt > stNode->getDueDate())
        continue; // Trễ station TW

      // Check 3: sau khi sạc partial, battery từ station đến customer
      // ⭐ Fix Phase2a-Check3: dùng partial charge thay vì full charge (battCap)
      // Route.evaluate() dùng partial charging: charge chỉ đủ để đến next node.
      // Estimate: charge đủ để đến customer = distStToCust * rate
      double distStToCust = instance->getDistance(stationId, customerId);
      double energyStToCust = distStToCust * consumptionRate;
      // Charge needed just to reach customer from station
      double chargeNeededToReach = std::max(0.0, energyStToCust - std::max(0.0, battAtSt));
      chargeNeededToReach = std::min(chargeNeededToReach, battCap - std::max(0.0, battAtSt));
      double battAfterCharge = std::max(0.0, battAtSt) + chargeNeededToReach;
      double battAtCustViaStation = battAfterCharge - energyStToCust;
      if (battAtCustViaStation < -1e-9)
        continue; // Không đến được customer từ station

      // Check 4: TW của customer qua station — dùng partial charge estimate
      // ⭐ Fix: chargeNeeded dùng chargeNeededToReach (partial) thay vì full charge
      auto stNodeTyped = std::static_pointer_cast<Station>(stNode);
      double chargeTime = chargeNeededToReach * stNodeTyped->getChargingRate();
      double departFromSt = std::max(arrAtSt, stNode->getReadyTime()) + chargeTime;
      double arrAtCustViaStation = departFromSt + instance->getTime(stationId, customerId);
      auto custNodeCheck = instance->getNodeById(customerId);
      if (arrAtCustViaStation > custNodeCheck->getDueDate())
        continue; // Trễ customer TW

      // Qua tất cả O(1) checks → deep copy để verify đầy đủ
      Route testRoute = route;
      testRoute.addNode(stationId, pos);      // station at pos first
      testRoute.addNode(customerId, pos + 1); // customer after station
      testRoute.evaluate();
      if (!testRoute.isFeasible())
        continue;

      double deltaDist = testRoute.getTotalDistance() - route.getTotalDistance();
      double deltaTime = testRoute.getTotalTime() - route.getTotalTime();
      double costMetric = deltaDist * 0.5 + deltaTime * 0.3;

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
    // ⭐ Fix fall-through: khi battAtCustDirect < 0, Phase 2a thử station-before.
    // KHÔNG return sớm — tiếp tục Phase 2b để tìm station-after opportunity.
    // (return bị xóa ở đây)
  }

  // --- Proceed with direct reachability logic / Phase 2b ---
  // Nếu battAtCustDirect < 0, custNode sẽ không pass checkInsertionCost —
  // nhưng Phase 2b vẫn có thể tìm station-after hợp lệ qua evaluateStationMove.
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
  // Energy is insufficient to reach next node OR we want to find a Free Charge
  // opportunity. ── Nâng Cấp 4: Expanded Station Search ── Tìm KNN stations ở 3
  // vùng: Gần Customer, Gần Next Node, và vùng Giữa
  auto custNodeBase = instance->getNodeById(customerId);
  auto nextNodeBase = instance->getNodeById(nextNodeId);
  double midX = (custNodeBase->getX() + nextNodeBase->getX()) / 2.0;
  double midY = (custNodeBase->getY() + nextNodeBase->getY()) / 2.0;

  std::vector<int> stNearCust =
      findKNearestStations(custNodeBase->getX(), custNodeBase->getY(), 3);
  std::vector<int> stNearMid = findKNearestStations(midX, midY, 3);
  std::vector<int> stNearNext =
      findKNearestStations(nextNodeBase->getX(), nextNodeBase->getY(), 3);

  std::vector<int> candidateStations;
  std::unordered_set<int> seenStations;
  auto addUnique = [&](const std::vector<int> &stations) {
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
      // ⭐ Fix cost formula: tránh unit mismatch km + phút.
      // Dùng detourDist (km) làm primary cost,
      // totalChargeTime (phút) được scale bằng vehicle speed estimate (~1 km/phút)
      // để convert về km-equivalent. Đây là approximation hợp lý cho EVRPTW.
      // Free Charge: bonus âm để ưu tiên tuyệt đối (không tốn time thực tế).
      const double TIME_TO_DIST_FACTOR = 1.0; // 1 phút ≈ 1 đơn vị distance (scale với instance)
      double baseCost = res.detourDist + res.totalChargeTime * TIME_TO_DIST_FACTOR;

      double costIncrease;
      if (res.isFreeCharge) {
        // Free Charge: station detour được "cover" bởi wait time → ưu tiên tối đa
        // costIncrease âm đảm bảo luôn được chọn so với non-free options
        costIncrease = -res.energyNeeded; // Bonus tỷ lệ năng lượng thu được
      } else if (res.energyNeeded < 1e-9) {
        // Charge lượng không đáng kể → chỉ tính detour cost
        costIncrease = baseCost;
      } else {
        // Normal case: detour + charge cost, trừ đi "value" của energy được nạp
        // Energy value = giảm risk phải thêm station khác về sau
        double energyValue = res.energyNeeded * 0.5; // heuristic weight
        costIncrease = baseCost - energyValue;
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
double SmartTimeAwareStationRepair::computeTimeRipple(const Route &route,
                                                      int customerId,
                                                      size_t pos) const {
  const auto &states = route.getStates();
  const auto &nodes = route.getNodes();
  if (pos >= nodes.size() || states.empty())
    return 0.0;

  // Time delay caused by inserting customer at pos
  int prevId = nodes[pos - 1];
  int nextId = nodes[pos];
  double oldDirectTime = instance->getTime(prevId, nextId);

  auto custNode = instance->getNodeById(customerId);
  double newTime = instance->getTime(prevId, customerId) +
                   custNode->getServiceTime() +
                   instance->getTime(customerId, nextId);

  double arrAtCust =
      states[pos - 1].departureTime + instance->getTime(prevId, customerId);
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
    double currentWait =
        std::max(0.0, node->getReadyTime() - states[i].arrivalTime);
    double newWait = std::max(0.0, node->getReadyTime() - newArr);

    cumulativeDelay -=
        (currentWait - newWait); // Reduction in wait time absorbs delay
    cumulativeDelay = std::max(0.0, cumulativeDelay);
  }

  return ripple;
}

} // namespace alns