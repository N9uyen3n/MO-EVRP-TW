#pragma once

#include "../../IOperator.h"
#include "../../../../include/core/Instance.h"
#include "../../../../include/core/Solution.h"
#include "../../../../include/core/Route.h"
#include <memory>
#include <random>
#include <vector>

// ============================================================================
// EnergyCriticalRemoval - Remove customers causing battery depletion risk
// ============================================================================
// Identifies customers that, if removed, would significantly reduce battery
// depletion risk on their route (i.e., customers that force excessive charging)
// Score = (energyToDepotAfterRemoval - currentBattery) - higher score = more critical
// ============================================================================
class EnergyCriticalRemoval : public IDestroyOperator {
public:
    EnergyCriticalRemoval(std::shared_ptr<Instance> instance, double threshold = 0.3);

    std::string getName() const override;

    std::vector<int> execute(Solution& solution, int nodesToRemove, std::mt19937& rng) override;

private:
    std::shared_ptr<Instance> instance;
    double threshold_;

    // Calculate energy risk score for a customer
    double calculateEnergyRiskScore(int customerId, const Route& route) const;

    // Find customers that cause critical battery depletion
    std::vector<int> findCriticalCustomers(const Solution& solution) const;
};