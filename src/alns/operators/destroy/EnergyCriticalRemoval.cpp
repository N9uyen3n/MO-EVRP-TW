#include "../../../../include/alns/operators/destroy/EnergyCriticalRemoval.h"
#include "../../../../include/core/Instance.h"
#include "../../../../include/core/Solution.h"
#include "../../../../include/core/Route.h"
#include "../../../../include/core/Customer.h"
#include "../../../../include/core/Station.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

// ============================================================================
// EnergyCriticalRemoval Implementation
// ============================================================================

EnergyCriticalRemoval::EnergyCriticalRemoval(std::shared_ptr<Instance> instance, double threshold)
 : instance(instance), threshold_(threshold) {}

std::string EnergyCriticalRemoval::getName() const {
 return "EnergyCriticalRemoval";
}

// Calculate energy risk score for a customer
double EnergyCriticalRemoval::calculateEnergyRiskScore(int customerId, const Route& route) const {
 const auto& customers = route.getCustomers();
 auto it = std::find(customers.begin(), customers.end(), customerId);
 if (it == customers.end()) return 0.0;

 size_t pos = std::distance(customers.begin(), it);

 const auto& states = route.getStates();
 if (states.size() <= pos + 1) return 0.0;

 double currentBattery = states[pos].remainingBattery;

 // Depot is always node 0
 double distToDepot = instance->getDistance(customerId, 0);
 double energyToDepot = distToDepot * instance->getVehicleEnergyRate();

 double energyToNext = 0.0;
 if (pos + 1 < customers.size()) {
  int nextId = customers[pos + 1];
  energyToNext = instance->getDistance(customerId, nextId) * instance->getVehicleEnergyRate();
 } else {
  energyToNext = energyToDepot;
 }

 double energyAfterRemoval = currentBattery - energyToNext;
 return std::max(0.0, energyToDepot - energyAfterRemoval);
}

// Find customers that cause critical battery depletion
std::vector<int> EnergyCriticalRemoval::findCriticalCustomers(const Solution& solution) const {
 std::vector<int> criticalCustomers;

 for (const auto& route : solution.getRoutes()) {
  if (route.getCustomers().empty()) continue;

  // Calculate average energy risk across route
  double totalRisk = 0.0;
  int customerCount = 0;

  for (int customerId : route.getCustomers()) {
   // Skip depot (node 0) - it should never be removed
   if (customerId == 0) continue;
   double riskScore = calculateEnergyRiskScore(customerId, route);
   totalRisk += riskScore;
   customerCount++;
  }

  if (customerCount == 0) continue;

  double avgRisk = totalRisk / customerCount;

  // Select customers with risk above threshold
  for (int customerId : route.getCustomers()) {
   // Skip depot (node 0) - it should never be removed
   if (customerId == 0) continue;
   double riskScore = calculateEnergyRiskScore(customerId, route);
   if (riskScore > avgRisk * threshold_) {
    criticalCustomers.push_back(customerId);
   }
  }
 }

 return criticalCustomers;
}

std::vector<int> EnergyCriticalRemoval::execute(Solution& solution, int nodesToRemove, std::mt19937& rng) {
 std::vector<int> removed;
 std::vector<int> criticalCustomers = findCriticalCustomers(solution);
 if (criticalCustomers.empty()) return removed;

 std::uniform_int_distribution<> dist(0, criticalCustomers.size() - 1);
 int customerIdToRemove = criticalCustomers[dist(rng)];

 // Safety check: never remove depot
 if (customerIdToRemove == 0) return removed;

 for (auto& route : solution.getRoutes()) {
  const auto& nodes = route.getNodes();
  for (size_t pos = 1; pos < nodes.size() - 1; ++pos) {
   if (nodes[pos] == customerIdToRemove) {
    route.removeNode(pos);
    route.evaluate();
    removed.push_back(customerIdToRemove);
    return removed;
   }
  }
 }
 return removed;
}