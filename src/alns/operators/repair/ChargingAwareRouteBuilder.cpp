#include "../../../../include/alns/operators/repair/ChargingAwareRouteBuilder.h"

// ============================================================================
// CHANGES REQUIRING .h UPDATES — ChargingAwareRouteBuilder.h
// ============================================================================
//
// [H-1] InnerState: thêm field mới
//   struct InnerState {
//     ...
//     bool lastWasStation = false;  // [FEATURE-1] ngăn relay-chain liên tiếp
//   };
//
// [H-2] Thêm 2 hằng số vào class (private static constexpr):
//   static constexpr int   MAX_RELAY_STATIONS = 2;
//       // Số trạm relay tối đa thử cho mỗi (innerState, custId) pair.
//       // = 2 đủ để bắt trạm tốt nhất & nhì mà không bùng beam quá mức.
//
//   static constexpr double LOOKAHEAD_WEIGHT = 0.15;
//       // Trọng số lookahead penalty trong pruneScore (Feature-2).
//       // Nhỏ (0.1–0.2) để không lấn át coverage objective chính.
//       // Tăng lên ~0.25 nếu muốn ưu tiên station gần cluster tiếp mạnh hơn.
//
// ============================================================================

#include "core/Customer.h"
#include "core/Route.h"
#include "core/Solution.h"
#include "core/Station.h"
#include "core/Vehicle.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace alns {

// ============================================================================
// Constructor
// ============================================================================

ChargingAwareRouteBuilder::ChargingAwareRouteBuilder(
    std::shared_ptr<Instance> instance)
    : instance_(std::move(instance)), avgDemand_(0.0) {

  // Pre-compute avgDemand
  const auto &customers = instance_->getCustomers();
  if (!customers.empty()) {
    double totalDemand = 0.0;
    for (const auto &c : customers)
      totalDemand += c->getDemand();
    avgDemand_ = totalDemand / static_cast<double>(customers.size());
  }
  avgDemand_ = std::max(avgDemand_, 1.0); // safety

  // Cache station IDs
  for (const auto &s : instance_->getStations()) {
    stationIds_.push_back(s->getId());
  }
}

// ============================================================================
// Helper: buildRouteFromSequence
// ============================================================================

Route ChargingAwareRouteBuilder::buildRouteFromSequence(
    const std::vector<int> &seq, int routeId) const {
  const double vehCap = instance_->getVehicleCapacity();
  const double vehBattery = instance_->getVehicleBattery();
  const double vehEnergy = instance_->getVehicleEnergyRate();

  // FIX #5: Dung routeId duoc truyen vao thay vi luon gán 0
  // Truoc day moi route co ID=0 gay xung dot khi nhieu route dung chung ID
  auto vehicle = std::make_shared<Vehicle>(routeId, vehCap, vehBattery, vehEnergy);
  Route route(routeId, vehicle, instance_);

  // seq[0] = depot, seq.back() = depot
  for (int i = 1; i < static_cast<int>(seq.size()) - 1; ++i) {
    route.addNode(seq[i], route.getNodes().size() - 1);
  }
  route.evaluate();
  return route;
}

// ============================================================================
// Helper: findNearestStation
// ============================================================================

int ChargingAwareRouteBuilder::findNearestStation(int nodeId) const {
  if (stationIds_.empty())
    return -1;
  int best = stationIds_[0];
  double minD = instance_->getDistance(nodeId, best);
  for (int sid : stationIds_) {
    double d = instance_->getDistance(nodeId, sid);
    if (d < minD) {
      minD = d;
      best = sid;
    }
  }
  return best;
}

// ============================================================================
// Helper: totalDemandOf
// ============================================================================

double ChargingAwareRouteBuilder::totalDemandOf(
    const std::vector<int> &customers) const {
  double total = 0.0;
  for (int id : customers)
    total += instance_->getNodeById(id)->getDemand();
  return total;
}

// ============================================================================
// generateSegmentsFrom
// ============================================================================

std::vector<ChargingAwareRouteBuilder::Segment>
ChargingAwareRouteBuilder::generateSegmentsFrom(
    int anchorId, const std::unordered_set<int> &remainingCusts,
    const OuterState &state) const {

  const double Q_MAX = instance_->getVehicleBattery();
  const double energyRate = instance_->getVehicleEnergyRate();
  const int depotId = 0;

  const int maxDepth =
      std::min(static_cast<int>(remainingCusts.size()),
               std::min(MAX_DEPTH_CAP,
                        std::max(1, static_cast<int>(std::floor(
                                        state.currentLoad / avgDemand_)))));

  std::vector<Segment> segments;

  if (remainingCusts.empty() || maxDepth <= 0)
    return segments;

  std::vector<int> exitAnchors = {depotId};
  for (int sid : stationIds_)
    exitAnchors.push_back(sid);

  InnerState init;
  init.position = anchorId;
  init.battery = state.currentBattery;
  init.time = state.currentTime;
  init.load = state.currentLoad;
  init.cost = 0.0;

  std::vector<InnerState> innerBeam = {init};

  for (int depth = 1; depth <= maxDepth; ++depth) {
    std::vector<InnerState> nextInnerBeam;

    for (const auto &innerState : innerBeam) {
      std::unordered_set<int> visitedSet(innerState.visited.begin(),
                                         innerState.visited.end());

      for (int custId : remainingCusts) {
        if (visitedSet.count(custId))
          continue;

        auto cust = instance_->getNodeById(custId);

        const double distToCust =
            instance_->getDistance(innerState.position, custId);
        const double energyToCust = distToCust * energyRate;
        const double battAtCust = innerState.battery - energyToCust;

        // ── Direct arc (unchanged path) ──────────────────────────────────────
        if (battAtCust >= 0.0) {
          const double arrivalAtCust =
              innerState.time + instance_->getTime(innerState.position, custId);
          if (arrivalAtCust <= cust->getDueDate()) {
            const double newLoad = innerState.load - cust->getDemand();
            if (newLoad >= 0.0) {
              bool canReachAny = false;
              for (int exitId : exitAnchors) {
                if (battAtCust >=
                    instance_->getDistance(custId, exitId) * energyRate) {
                  canReachAny = true;
                  break;
                }
              }
              if (canReachAny) {
                const double serviceStart =
                    std::max(arrivalAtCust, cust->getReadyTime());
                InnerState newInner;
                newInner.position = custId;
                newInner.battery = battAtCust;
                newInner.time = serviceStart + cust->getServiceTime();
                newInner.load = newLoad;
                newInner.visited = innerState.visited;
                newInner.visited.push_back(custId);
                newInner.cost = innerState.cost + distToCust;
                nextInnerBeam.push_back(std::move(newInner));
              }
            }
          }
        }

        // ── [FEATURE-1] Multi-Station Relay: pos → station → custId ──────────
        // Chỉ kích hoạt khi:
        //   (a) battery không đủ để đi thẳng TỚI custId, HOẶC
        //   (b) battery đủ tới custId nhưng không đủ thoát ra EXIT nào sau đó
        // Giới hạn: không relay nếu innerState đã relay ở bước này
        //           (tránh chuỗi station-station vô hạn trong inner beam).
        // Cap: thử tối đa MAX_RELAY_STATIONS trạm có detour nhỏ nhất.
        {
          const bool needRelay = (battAtCust < 0.0) ||
                                 [&]() {
                                   if (battAtCust < 0.0) return false;
                                   for (int exitId : exitAnchors)
                                     if (battAtCust >= instance_->getDistance(
                                                           custId, exitId) *
                                                           energyRate)
                                       return false;
                                   return true;
                                 }();

          // Chỉ relay nếu chưa relay ở step này (lastWasStation flag)
          if (needRelay && !innerState.lastWasStation) {
            const double newLoad = innerState.load - cust->getDemand();
            if (newLoad >= 0.0) {
              // Tìm top-MAX_RELAY_STATIONS stations theo tổng detour
              // pos→station + station→cust (không dùng dist trực tiếp để
              // phản ánh chi phí thực khi ghé trạm trên đường)
              struct RelayCand { int sid; double detour; };
              std::vector<RelayCand> relayCands;
              relayCands.reserve(stationIds_.size());
              const double directDist =
                  instance_->getDistance(innerState.position, custId);
              for (int sid : stationIds_) {
                // Chỉ xét station không phải anchor hiện tại (tránh vòng)
                if (sid == innerState.position) continue;
                double det =
                    instance_->getDistance(innerState.position, sid) +
                    instance_->getDistance(sid, custId) - directDist;
                relayCands.push_back({sid, det});
              }
              int nCands = std::min(MAX_RELAY_STATIONS,
                                    static_cast<int>(relayCands.size()));
              std::partial_sort(
                  relayCands.begin(), relayCands.begin() + nCands,
                  relayCands.end(),
                  [](const RelayCand &a, const RelayCand &b) {
                    return a.detour < b.detour;
                  });

              for (int ri = 0; ri < nCands; ++ri) {
                int sid = relayCands[ri].sid;
                auto stNode = instance_->getNodeById(sid);

                // Leg 1: pos → station
                const double distToSt =
                    instance_->getDistance(innerState.position, sid);
                const double battAfterSt =
                    innerState.battery - distToSt * energyRate;
                if (battAfterSt < 0.0) continue;

                const double arrSt =
                    innerState.time +
                    instance_->getTime(innerState.position, sid);
                if (arrSt > stNode->getDueDate()) continue;

                // Charge to full at station
                const double chargeNeeded = Q_MAX - battAfterSt;
                double chargeTime = 0.0;
                if (stNode->getType() == NodeType::STATION) {
                  chargeTime =
                      chargeNeeded *
                      std::static_pointer_cast<Station>(stNode)
                          ->getChargingRate();
                }
                const double depSt =
                    std::max(arrSt, stNode->getReadyTime()) + chargeTime;

                // Leg 2: station → custId
                const double distStCust =
                    instance_->getDistance(sid, custId);
                const double battAtCust2 =
                    Q_MAX - distStCust * energyRate;
                if (battAtCust2 < 0.0) continue;

                const double arrCust2 =
                    depSt + instance_->getTime(sid, custId);
                if (arrCust2 > cust->getDueDate()) continue;

                // Can we escape from custId?
                bool canEscape = false;
                for (int exitId : exitAnchors) {
                  if (battAtCust2 >= instance_->getDistance(custId, exitId) *
                                         energyRate) {
                    canEscape = true;
                    break;
                  }
                }
                if (!canEscape) continue;

                const double svcStart2 =
                    std::max(arrCust2, cust->getReadyTime());
                InnerState relayState;
                relayState.position = custId;
                relayState.battery = battAtCust2;
                relayState.time = svcStart2 + cust->getServiceTime();
                relayState.load = newLoad;
                relayState.visited = innerState.visited;
                relayState.visited.push_back(custId);
                // cost tính đủ cả hai leg (không tính chargeTime vì cost là
                // khoảng cách, không phải thời gian)
                relayState.cost =
                    innerState.cost + distToSt + distStCust;
                relayState.lastWasStation = true; // ngăn relay-chain
                nextInnerBeam.push_back(std::move(relayState));
              }
            }
          }
        }
      }

      if (!innerState.visited.empty()) {
        for (int exitId : exitAnchors) {
          const double distToExit =
              instance_->getDistance(innerState.position, exitId);
          const double energyToExit = distToExit * energyRate;
          if (innerState.battery < energyToExit)
            continue;

          const double arrivalAtExit =
              innerState.time + instance_->getTime(innerState.position, exitId);

          if (exitId != depotId) {
            auto exitNode = instance_->getNodeById(exitId);
            if (arrivalAtExit > exitNode->getDueDate())
              continue;
          }

          Segment seg;
          seg.entryAnchor = anchorId;
          seg.exitAnchor = exitId;
          seg.customers = innerState.visited;
          seg.cost = innerState.cost + distToExit;
          seg.arrivalAtExit = arrivalAtExit;
          seg.batteryAtExit = innerState.battery -
                              energyToExit; // FIX: battery thực tế tại exit
          segments.push_back(std::move(seg));
        }
      }
    }

    if (nextInnerBeam.empty())
      break;

    std::partial_sort(
        nextInnerBeam.begin(),
        nextInnerBeam.begin() +
            std::min(INNER_BEAM_WIDTH, static_cast<int>(nextInnerBeam.size())),
        nextInnerBeam.end(), [](const InnerState &a, const InnerState &b) {
          const double sa =
              a.cost / std::max(1, static_cast<int>(a.visited.size()));
          const double sb =
              b.cost / std::max(1, static_cast<int>(b.visited.size()));
          return sa < sb;
        });

    innerBeam = std::vector<InnerState>(
        nextInnerBeam.begin(),
        nextInnerBeam.begin() +
            std::min(INNER_BEAM_WIDTH, static_cast<int>(nextInnerBeam.size())));
  }

  return segments;
}

// ============================================================================
// buildOneRoute
// ============================================================================

Route ChargingAwareRouteBuilder::buildOneRoute(
    int depotId, int targetCust, const std::unordered_set<int> &remaining,
    std::mt19937 & /*rng*/) const {
  const double Q_MAX = instance_->getVehicleBattery();
  const double energyRate = instance_->getVehicleEnergyRate();
  auto depotNode = instance_->getNodeById(depotId);

  OuterState initOuter;
  initOuter.nodeSequence = {depotId};
  initOuter.currentAnchor = depotId;
  initOuter.currentTime = depotNode->getReadyTime();
  initOuter.currentBattery = Q_MAX;
  initOuter.currentLoad = instance_->getVehicleCapacity();
  initOuter.segmentCost = 0.0;
  initOuter.pruneScore = 0.0;

  std::vector<OuterState> outerBeam = {initOuter};

  struct CompletedRoute {
    Route route;
    std::unordered_set<int> covered;
    double cost;
  };
  std::vector<CompletedRoute> completedRoutes;

  const int maxSegments = static_cast<int>(stationIds_.size()) + 2;

  for (int segStep = 0; segStep < maxSegments; ++segStep) {
    std::vector<OuterState> nextOuterBeam;

    for (const auto &outerState : outerBeam) {
      std::unordered_set<int> localRemaining;
      for (int id : remaining) {
        if (!outerState.coveredCusts.count(id))
          localRemaining.insert(id);
      }

      auto segs = generateSegmentsFrom(outerState.currentAnchor, localRemaining,
                                       outerState);

      for (auto &seg : segs) {
        std::unordered_set<int> newCovered = outerState.coveredCusts;
        for (int cid : seg.customers)
          newCovered.insert(cid);
        const double newCost = outerState.segmentCost + seg.cost;

        std::vector<int> newSeq = outerState.nodeSequence;
        for (int cid : seg.customers)
          newSeq.push_back(cid);
        newSeq.push_back(seg.exitAnchor);

        if (seg.exitAnchor == depotId) {
          // FIX #5: Truyen routeId tang dan de tranh xung dot ID=0
          Route testRoute = buildRouteFromSequence(newSeq,
              static_cast<int>(completedRoutes.size()));
          if (testRoute.isFeasible()) {
            completedRoutes.push_back(
                {std::move(testRoute), newCovered, newCost});
          }
        } else {
          auto exitStation = instance_->getNodeById(seg.exitAnchor);

          // FIX: dùng batteryAtExit từ segment (đã tính đúng qua mọi customer)
          const double battAtExit = seg.batteryAtExit;
          const double chargeNeeded = Q_MAX - std::max(0.0, battAtExit);
          double chargeTime = 0.0;
          if (exitStation->getType() == NodeType::STATION) {
            chargeTime =
                chargeNeeded * std::static_pointer_cast<Station>(exitStation)
                                   ->getChargingRate();
          }

          OuterState newOuter;
          newOuter.nodeSequence = newSeq;
          newOuter.currentAnchor = seg.exitAnchor;
          newOuter.currentTime = seg.arrivalAtExit + chargeTime;
          newOuter.currentBattery = Q_MAX;
          newOuter.currentLoad =
              outerState.currentLoad - totalDemandOf(seg.customers);
          newOuter.coveredCusts = newCovered;
          newOuter.segmentCost = newCost;
          newOuter.pruneScore = 0.0;
          nextOuterBeam.push_back(std::move(newOuter));
        }
      }

      if (!outerState.coveredCusts.empty()) {
        std::vector<int> closeSeq = outerState.nodeSequence;
        closeSeq.push_back(depotId);
        // FIX #5: Truyen routeId tang dan
        Route testRoute = buildRouteFromSequence(closeSeq,
            static_cast<int>(completedRoutes.size()));
        if (testRoute.isFeasible()) {
          completedRoutes.push_back({std::move(testRoute),
                                     outerState.coveredCusts,
                                     outerState.segmentCost});
        }
      }
    }

    if (nextOuterBeam.empty())
      break;

    for (auto &os : nextOuterBeam) {
      const double baseScore =
          os.segmentCost /
          std::max(1.0, static_cast<double>(os.coveredCusts.size()));
      const double bonus =
          os.coveredCusts.count(targetCust) ? TARGET_BONUS : 0.0;

      // ── [FEATURE-2] Distance-Driven Charging lookahead ───────────────────
      // Khi outerState kết thúc tại một STATION (không phải depot), ước tính
      // "chất lượng của segment tiếp theo" bằng min-distance từ station đó đến
      // các remaining customers chưa covered.
      //
      // Trực giác: station S tốt là station mà các customer gần nhất với S là
      // những customer còn lại — tức là S nằm "trên đường" đến cluster tiếp,
      // không phải chỉ "tiện đường" so với cluster vừa qua.
      //
      // lookahead = (trọng số nhỏ) × min_dist(station → remaining_customers)
      // → station tốt hơn cho segment tiếp → lookaheadPenalty nhỏ hơn
      //   → pruneScore nhỏ hơn → được ưu tiên giữ lại trong beam.
      double lookaheadPenalty = 0.0;
      {
        // Chỉ tính lookahead khi anchor hiện tại là station (không phải depot)
        const bool isStation = [&]() {
          for (int sid : stationIds_)
            if (sid == os.currentAnchor) return true;
          return false;
        }();

        if (isStation && !remaining.empty()) {
          // Tìm min distance từ station tới các customer còn lại
          double minDistToRemaining = std::numeric_limits<double>::max();
          for (int rid : remaining) {
            if (os.coveredCusts.count(rid)) continue; // đã covered
            double d = instance_->getDistance(os.currentAnchor, rid);
            if (d < minDistToRemaining) minDistToRemaining = d;
          }
          if (minDistToRemaining < std::numeric_limits<double>::max()) {
            // Normalize: chia cho avgDistance để scale tương đương baseScore
            // Hệ số LOOKAHEAD_WEIGHT nhỏ để không lấn át coverage objective
            lookaheadPenalty = LOOKAHEAD_WEIGHT * minDistToRemaining;
          }
        }
      }

      os.pruneScore = baseScore - bonus + lookaheadPenalty;
    }

    std::partial_sort(
        nextOuterBeam.begin(),
        nextOuterBeam.begin() +
            std::min(OUTER_BEAM_WIDTH, static_cast<int>(nextOuterBeam.size())),
        nextOuterBeam.end(), [](const OuterState &a, const OuterState &b) {
          return a.pruneScore < b.pruneScore;
        });

    outerBeam = std::vector<OuterState>(
        nextOuterBeam.begin(),
        nextOuterBeam.begin() +
            std::min(OUTER_BEAM_WIDTH, static_cast<int>(nextOuterBeam.size())));
  }

  if (completedRoutes.empty()) {
    // Trả về một Route hợp lệ nhưng RỖNG (không có khách hàng) thay vì
    // Route(0, nullptr, nullptr) vì constructor sẽ gọi evaluate() và crash
    // khi dereference nullptr. Caller kiểm tra !route.getCustomers().empty()
    // để bỏ qua route rỗng này.
    const double vehCap = instance_->getVehicleCapacity();
    const double vehBat = instance_->getVehicleBattery();
    const double vehEng = instance_->getVehicleEnergyRate();
    auto emptyVehicle = std::make_shared<Vehicle>(0, vehCap, vehBat, vehEng);
    return Route(0, emptyVehicle, instance_);
  }

  auto bestIt = std::max_element(
      completedRoutes.begin(), completedRoutes.end(),
      [&](const CompletedRoute &a, const CompletedRoute &b) {
        if (a.covered.size() != b.covered.size())
          return a.covered.size() < b.covered.size();
        if (std::abs(a.cost - b.cost) > 1e-9)
          return a.cost > b.cost;
        return !a.covered.count(targetCust) && b.covered.count(targetCust);
      });

  return bestIt->route;
}

// ============================================================================
// Phase B helpers
// ============================================================================

ChargingAwareRouteBuilder::InsertCandidate
ChargingAwareRouteBuilder::findBestInsertForCustomer(
    int custId, std::vector<Route> &routes) const {

  InsertCandidate best;
  best.cost = 1e9;
  best.custId = custId;
  best.routeIdx = -1;
  const double demand = instance_->getNodeById(custId)->getDemand();
  const double vehCap = instance_->getVehicleCapacity();
  const auto& allStations = instance_->getStations();
  // FIX #2: Khong dung nearStat chung cho ca ham.
  // Thay bang: voi moi vi tri (r, pos), tim station min-detour cu the.

  for (int r = 0; r < static_cast<int>(routes.size()); ++r) {
    if (routes[r].getTotalDemand() + demand > vehCap)
      continue;

    for (size_t pos = 1; pos < routes[r].size(); ++pos) {
      InsertionResult res = routes[r].checkInsertionCost(custId, pos);
      if (res.isFeasible && res.deltaDistance < best.cost) {
        best = {custId, r, static_cast<int>(pos), res.deltaDistance, -1, true};
      }

      if (!res.isFeasible && !allStations.empty()) {
        // FIX #2: Tim station co min-detour cho tung vi tri (r, pos)
        const int prevId = routes[r].getNodes()[pos - 1];
        const int nextId = routes[r].getNodes()[pos];
        int bestStatB1 = -1; double minDetB1 = 1e18;
        int bestStatB2 = -1; double minDetB2 = 1e18;
        for (const auto& st : allStations) {
          int sid = st->getId();
          double detB1 = instance_->getDistance(prevId, sid)
                       + instance_->getDistance(sid, custId);
          double detB2 = instance_->getDistance(custId, sid)
                       + instance_->getDistance(sid, nextId);
          if (detB1 < minDetB1) { minDetB1 = detB1; bestStatB1 = sid; }
          if (detB2 < minDetB2) { minDetB2 = detB2; bestStatB2 = sid; }
        }
        // Pattern B1: [station, cust] — station truoc customer
        if (bestStatB1 != -1) {
          Route copy = routes[r];
          copy.addNode(custId, pos);
          copy.addNode(bestStatB1, pos);
          copy.evaluate();
          if (copy.isFeasible()) {
            const double dCost =
                copy.getTotalDistance() - routes[r].getTotalDistance();
            if (dCost < best.cost) {
              best = {custId, r, static_cast<int>(pos), dCost, bestStatB1, true};
            }
          }
        }
        // Pattern B2: [cust, station] — station sau customer
        if (bestStatB2 != -1) {
          Route copy2 = routes[r];
          copy2.addNode(custId, pos);
          copy2.addNode(bestStatB2, pos + 1);
          copy2.evaluate();
          if (copy2.isFeasible()) {
            const double dCost =
                copy2.getTotalDistance() - routes[r].getTotalDistance();
            if (dCost < best.cost) {
              best = {custId, r, static_cast<int>(pos), dCost, bestStatB2, false};
            }
          }
        }
      }
    }
  }
  return best;
}


void ChargingAwareRouteBuilder::applyCandidate(
    const InsertCandidate &c, std::vector<Route> &routes) const {
  if (c.stationId == -1) {
    routes[c.routeIdx].addNode(c.custId, c.pos);
  } else if (c.statBefore) {
    routes[c.routeIdx].addNode(c.custId, c.pos);
    routes[c.routeIdx].addNode(c.stationId, c.pos);
  } else {
    routes[c.routeIdx].addNode(c.custId, c.pos);
    routes[c.routeIdx].addNode(c.stationId, c.pos + 1);
  }
  routes[c.routeIdx].evaluate();
}

// ============================================================================
// execute Phase A + Phase B
// ============================================================================

void ChargingAwareRouteBuilder::execute(
    Solution &solution, const std::vector<int> &unservedCustomers,
    std::mt19937 &rng) {
  if (unservedCustomers.empty())
    return;

  const int depotId = 0;
  const double vehCap = instance_->getVehicleCapacity();
  const double vehBat = instance_->getVehicleBattery();
  const double vehEng = instance_->getVehicleEnergyRate();

  std::unordered_set<int> remaining(unservedCustomers.begin(),
                                    unservedCustomers.end());
  std::vector<Route> newRoutes;

  const int maxAttempts =
      std::min(static_cast<int>(remaining.size()), MAX_ATTEMPTS);
  int consecutiveFail = 0;

  for (int attempt = 0; attempt < maxAttempts && !remaining.empty();
       ++attempt) {
    int seedCust = *std::min_element(
        remaining.begin(), remaining.end(), [&](int a, int b) {
          auto na = instance_->getNodeById(a);
          auto nb = instance_->getNodeById(b);
          return (na->getDueDate() - na->getReadyTime()) <
                 (nb->getDueDate() - nb->getReadyTime());
        });

    Route route = buildOneRoute(depotId, seedCust, remaining, rng);

    if (route.isFeasible() && !route.getCustomers().empty()) {
      const auto &covered = route.getCustomers();
      for (int cid : covered)
        remaining.erase(cid);
      newRoutes.push_back(std::move(route));
      consecutiveFail = 0;
    } else {
      ++consecutiveFail;
      if (consecutiveFail >= FAIL_THRESHOLD)
        break;
    }
  }

  if (!remaining.empty()) {
    std::vector<int> hardCustomers(remaining.begin(), remaining.end());
    std::sort(hardCustomers.begin(), hardCustomers.end(), [&](int a, int b) {
      auto na = instance_->getNodeById(a);
      auto nb = instance_->getNodeById(b);
      return (na->getDueDate() - na->getReadyTime()) <
             (nb->getDueDate() - nb->getReadyTime());
    });

    std::vector<Route> allRoutes = newRoutes;
    for (const auto &sr : solution.getRoutes())
      allRoutes.push_back(sr);

    for (int custId : hardCustomers) {
      InsertCandidate best = findBestInsertForCustomer(custId, allRoutes);

      if (best.routeIdx != -1) {
        applyCandidate(best, allRoutes);
        remaining.erase(custId);
      } else {
        auto vehicle = std::make_shared<Vehicle>(0, vehCap, vehBat, vehEng);
        Route solo(0, vehicle, instance_);
        solo.addNode(custId, solo.getNodes().size() - 1);
        solo.evaluate();

        bool soloOk = solo.isFeasible();
        if (!soloOk) {
          // FIX #3: Moi pattern dung metric detour rieng phu hop:
          //   rAfter [cust, station, depot]: dist(cust, st) + dist(st, depot)
          //   rBefore [depot, station, cust, depot]: dist(depot, st) + dist(st, cust)
          // Sap xep theo min(detour_after, detour_before) de uu tien station tot nhat tong the
          struct StDet { int id; double detAfter; double detBefore; };
          std::vector<StDet> sortedStats;
          const int depotId0 = 0;
          for (int sid : stationIds_) {
            double detAfter  = instance_->getDistance(custId, sid)
                             + instance_->getDistance(sid, depotId0);
            double detBefore = instance_->getDistance(depotId0, sid)
                             + instance_->getDistance(sid, custId);
            sortedStats.push_back({sid, detAfter, detBefore});
          }
          // Sap xep theo min cua ca hai de dam bao station tot nhat duoc thu truoc
          std::sort(sortedStats.begin(), sortedStats.end(),
                    [](const StDet& a, const StDet& b){
                      return std::min(a.detAfter, a.detBefore)
                           < std::min(b.detAfter, b.detBefore);
                    });

          for (const auto& sd : sortedStats) {
            // Thu pattern rAfter: [cust, station] theo detour cust→st→depot
            Route rAfter = solo;
            rAfter.addNode(sd.id, 2);
            rAfter.evaluate();
            if (rAfter.isFeasible()) {
              solo = rAfter;
              soloOk = true;
              break;
            }
            // Thu pattern rBefore: [station, cust] theo detour depot→st→cust
            Route rBefore = solo;
            rBefore.addNode(sd.id, 1);
            rBefore.evaluate();
            if (rBefore.isFeasible()) {
              solo = rBefore;
              soloOk = true;
              break;
            }
          }
        }

        if (soloOk) {
          allRoutes.push_back(solo);
          remaining.erase(custId);
        }
      }
    }

    // Rebuild solution routes từ allRoutes (tránh self-assignment và index
    // shift)
    solution.clear();
    for (auto &r : allRoutes) {
      if (r.isFeasible() && !r.getCustomers().empty()) {
        solution.addRoute(r);
      }
    }
    // Không cần commit thêm bên dưới — đã xong
    return;
  }

  for (auto &route : newRoutes) {
    route.evaluate();
    if (route.isFeasible() && !route.getCustomers().empty()) {
      solution.addRoute(route);
    }
  }

  solution.evaluateRoutes();
}

} // namespace alns