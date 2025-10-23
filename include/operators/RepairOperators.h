// include/operators/RepairOperators.h
#pragma once
#include "../Solver.h"
#include "../Customer.h"
#include "../Instance.h"
#include <vector>
#include <memory>
#include <random>
#include <string>

class IRepairOperator {
public:
    virtual ~IRepairOperator() = default;
    virtual bool repair(Solution& solution, const Instance& instance, std::vector<std::shared_ptr<Customer>>& unassigned, std::mt19937& rng) = 0;
    virtual std::string getName() const = 0;
};

// 1. Greedy Insertion
class GreedyInsertion : public IRepairOperator {
public:
    bool repair(Solution& solution, const Instance& instance, std::vector<std::shared_ptr<Customer>>& unassigned, std::mt19937& rng) override;
    std::string getName() const override { return "Greedy Insertion"; }
};

// 2. Regret Insertion
class RegretInsertion : public IRepairOperator {
private:
    int k; // Regret-k
public:
    explicit RegretInsertion(int regret_k = 3) : k(regret_k) {}
    bool repair(Solution& solution, const Instance& instance, std::vector<std::shared_ptr<Customer>>& unassigned, std::mt19937& rng) override;
    std::string getName() const override { return "Regret-" + std::to_string(k) + " Insertion"; }
};
