// include/operators/DestroyOperators.h
#pragma once
#include "../Solver.h"
#include "../Customer.h"
#include "../Instance.h"
// #include "../core/ServiceLocator.h" // Removed
#include <vector>
#include <random>
#include <algorithm>

class IDestroyOperator {
public:
    virtual ~IDestroyOperator() = default;
    virtual std::vector<std::shared_ptr<Customer>> destroy(Solution& solution, const Instance& instance, double destructionRate, std::mt19937& rng) = 0;
    virtual std::string getName() const = 0;
};

// 1. Random Removal
class RandomRemoval : public IDestroyOperator {
public:
    std::vector<std::shared_ptr<Customer>> destroy(Solution& solution, const Instance& instance, double rate, std::mt19937& rng) override;
    std::string getName() const override { return "Random Removal"; }
};

// 2. Worst Distance Removal
class WorstDistanceRemoval : public IDestroyOperator {
public:
    std::vector<std::shared_ptr<Customer>> destroy(Solution& solution, const Instance& instance, double rate, std::mt19937& rng) override;
    std::string getName() const override { return "Worst Distance Removal"; }
};

// 3. Shaw Removal (relatedness-based)
class ShawRemoval : public IDestroyOperator {
private:
    double relatedness(const Customer* c1, const Customer* c2, const Instance& instance) const;
public:
    std::vector<std::shared_ptr<Customer>> destroy(Solution& solution, const Instance& instance, double rate, std::mt19937& rng) override;
    std::string getName() const override { return "Shaw Removal"; }
};

// 4. Zone Removal
class ZoneRemoval : public IDestroyOperator {
public:
    std::vector<std::shared_ptr<Customer>> destroy(Solution& solution, const Instance& instance, double rate, std::mt19937& rng) override;
    std::string getName() const override { return "Zone Removal"; }
};
