#ifndef ALNS_H
#define ALNS_H

#include "Solver.h"
#include "Instance.h"
#include "objectives/ObjectiveFunction.h"
#include "operators/DestroyOperators.h"
#include "operators/RepairOperators.h"
#include <vector>
#include <memory>
#include <random>
#include <utility> // For std::pair

class ALNS : public Solver {
public:
    explicit ALNS(ObjectiveManager manager, std::shared_ptr<Instance> instance);
    explicit ALNS(ObjectiveManager manager, 
                  std::shared_ptr<Instance> instance,
                    double sigma1, 
                    double sigma2, 
                    double sigma3,
                  int maxIterations,
                  int segmentSize,
                  double destructionRate,
                  double initialTemperature,
                  double coolingRate,
                  double reactionFactor);
    std::vector<Solution> solve() override;

private:
    std::shared_ptr<Instance> instance;

    // ALNS Configuration
    int maxIterations;
    int segmentSize;
    double destructionRate;
    
    // Simulated Annealing Parameters
    double temperature;
    double coolingRate;
    double minimumTemperature;
    double initialTemperature; // For reheating

    // Operator Weight Update
    double reactionFactor; 

    // Rewards for scores
    double sigma1, sigma2, sigma3; 

    ObjectiveManager objectiveManager;
    std::vector<Solution> archive;
    std::mt19937 rng; // Random Number Generator

    // Operator Control
    std::vector<std::unique_ptr<IDestroyOperator>> destroyOperators;
    std::vector<std::unique_ptr<IRepairOperator>> repairOperators;
    std::vector<double> destroyWeights;
    std::vector<double> repairWeights;
    std::vector<size_t> destroyScores;
    std::vector<size_t> repairScores;
    std::vector<size_t> destroyUses;
    std::vector<size_t> repairUses;
    std::vector<size_t> destroyBestScores;
    std::vector<size_t> repairBestScores;

    // Helper methods for the ALNS algorithm
    void initializeOperators();
    Solution generateInitialSolution();
    bool updateArchive(Solution& newSolution); // Return true if solution is added
    bool isFeasible(const Solution& solution);
    void calculateObjectives(Solution& solution);
    std::pair<IDestroyOperator*, size_t> selectOperator(const std::vector<std::unique_ptr<IDestroyOperator>>& operators, const std::vector<double>& weights);
    std::pair<IRepairOperator*, size_t> selectOperator(const std::vector<std::unique_ptr<IRepairOperator>>& operators, const std::vector<double>& weights);
    void updateWeights();
    void applyLocalSearch(Solution& solution); // <--- THÊM DÒNG NÀY

    // Dominance check
    enum Dominance { DOMINATES, DOMINATED, NON_DOMINATED };
    Dominance dominanceCheck(const Solution& a, const Solution& b);

    // Logging
    void printHeader() const;
    void logProgress(int iteration, int accepted, int rejected, int feasible, int infeasible) const;
    void printFooter(long long duration, int accepted, int rejected, int feasible, int infeasible) const;
};

#endif // ALNS_H