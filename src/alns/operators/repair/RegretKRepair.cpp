#include "../../../../include/alns/operators/repair/RegretKRepair.h"
#include <algorithm>
#include <vector>
#include <cmath>

RegretKRepair::RegretKRepair(std::shared_ptr<Instance> instance, int k, double noiseParameter)
    : instance(instance), k_regret(k), noiseParam(noiseParameter) {}

std::string RegretKRepair::getName() const {
    return "Regret-" + std::to_string(k_regret) + " Repair";
}

// Helper function
namespace {
void createNewRouteForCustomer(Solution& solution, int customerId, std::shared_ptr<Instance> instance) {
    int newRouteId = solution.getNumRoutes();
    auto vehicle = std::make_shared<Vehicle>(
        newRouteId,
        instance->getVehicleCapacity(),
        instance->getVehicleBattery(),
        instance->getVehicleEnergyRate()
    );
    Route newRoute(newRouteId, vehicle, instance);
    newRoute.addNode(customerId, 1);
    newRoute.evaluate();
    solution.addRoute(newRoute);
}
}


void RegretKRepair::execute(Solution& solution, const std::vector<int>& unservedCustomers, std::mt19937& rng) {
    std::vector<int> remainingCustomers = unservedCustomers;

    while (!remainingCustomers.empty()) {
        int bestCustId = -1;
        int bestRouteIdx = -1;
        int bestPos = -1;
        double maxRegret = -1.0;

        for (int custId : remainingCustomers) {
            std::vector<InsertionCost> kBest = findKBestInsertions(custId, solution, rng);

            if (kBest.empty()) continue;

            double regretVal = 0.0;
            double bestCost = kBest[0].cost;

            if (kBest.size() >= k_regret) {
                for(size_t i = 1; i < k_regret; ++i) {
                    regretVal += (kBest[i].cost - bestCost);
                }
            } else {
                regretVal = std::numeric_limits<double>::max(); 
            }

            if (regretVal > maxRegret) {
                maxRegret = regretVal;
                bestCustId = custId;
                bestRouteIdx = kBest[0].routeIndex;
                bestPos = kBest[0].position;
            }
        }

        if (bestCustId != -1) {
            solution.getRoutes()[bestRouteIdx].addNode(bestCustId, bestPos);
            solution.getRoutes()[bestRouteIdx].evaluate();
            remainingCustomers.erase(std::remove(remainingCustomers.begin(), remainingCustomers.end(), bestCustId), remainingCustomers.end());
        } else {
            // If no customer could be inserted, create a new route for the first one
            if (!remainingCustomers.empty()) {
                createNewRouteForCustomer(solution, remainingCustomers[0], instance);
                remainingCustomers.erase(remainingCustomers.begin());
            }
        }
    }
}

std::vector<RegretKRepair::InsertionCost> RegretKRepair::findKBestInsertions(int customerId, Solution& solution, std::mt19937& rng) {
    
    struct Candidate {
        int routeIdx;
        size_t position;
        double estimatedCost;
        bool operator<(const Candidate& other) const {
            return estimatedCost < other.estimatedCost;
        }
    };

    std::vector<Candidate> candidates;
    auto& routes = solution.getRoutes();

    // Tier 1 & 2: Generate candidates with fast, approximate checks
    for (int r = 0; r < routes.size(); ++r) {
        for (size_t pos = 1; pos < routes[r].getNodes().size(); ++pos) {
            if (!routes[r].canPossiblyInsert(customerId, pos)) {
                continue;
            }
            auto fastResult = routes[r].fastForwardCheck(customerId, pos);
            if (fastResult.isFeasible) {
                candidates.push_back({r, pos, fastResult.deltaDistance});
            }
        }
    }

    if (candidates.empty()) {
        return {};
    }

    std::sort(candidates.begin(), candidates.end());

    // Tier 3: Verify top candidates with exact cost
    std::vector<InsertionCost> exactInsertions;
    const int VERIFY_COUNT = k_regret * 2; // Verify more candidates than needed

    for (int i = 0; i < std::min((int)candidates.size(), VERIFY_COUNT); ++i) {
        auto& candidate = candidates[i];
        auto exactResult = routes[candidate.routeIdx].checkInsertionCost(customerId, candidate.position);

        if (exactResult.isFeasible) {
            double baseCost = exactResult.deltaDistance;
            if (noiseParam > 0) {
                std::uniform_real_distribution<double> dist(-noiseParam, noiseParam);
                baseCost *= (1.0 + dist(rng));
                if (baseCost < 0) baseCost = 0;
            }
            exactInsertions.push_back({candidate.routeIdx, candidate.position, baseCost, true});
        }
    }

    if (exactInsertions.empty()) {
        return {};
    }

    // Sort again based on exact costs
    std::sort(exactInsertions.begin(), exactInsertions.end(), [](const InsertionCost& a, const InsertionCost& b) {
        return a.cost < b.cost;
    });

    // Return only the top k results
    if (exactInsertions.size() > k_regret) {
        exactInsertions.resize(k_regret);
    }

    return exactInsertions;
}
