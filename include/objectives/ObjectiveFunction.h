#pragma once
#include "../Solver.h"
#include "../Route.h"
#include <string>
#include <vector>
#include <memory>
#include <algorithm>

class IObjective {
public:
    virtual ~IObjective() = default;
    virtual double evaluate(const Solution& solution) const = 0;
    virtual std::string getName() const = 0;
    virtual bool minimize() const = 0; // true = minimize, false = maximize
};

class NumberOfVehiclesObjective : public IObjective {
public:
    double evaluate(const Solution& solution) const override {
        return solution.routes.size();
    }
    
    std::string getName() const override { return "Number of Vehicles"; }
    bool minimize() const override { return true; }
};

class TotalDistanceObjective : public IObjective {
public:
    double evaluate(const Solution& solution) const override {
        double total = 0.0;
        for (const auto& route : solution.routes) {
            total += route.getTotalDistance();
        }
        return total;
    }
    
    std::string getName() const override { return "Total Distance"; }
    bool minimize() const override { return true; }
};

class TotalEnergyObjective : public IObjective {
public:
    double evaluate(const Solution& solution) const override {
        double total = 0.0;
        for (const auto& route : solution.routes) {
            total += route.getTotalEnergyCharged();
        }
        return total;
    }
    
    std::string getName() const override { return "Total Energy Cost"; }
    bool minimize() const override { return true; }
};

class MakespanObjective : public IObjective {
public:
    double evaluate(const Solution& solution) const override {
        double maxTime = 0.0;
        for (const auto& route : solution.routes) {
            maxTime = std::max(maxTime, route.getTotalTime());
        }
        return maxTime;
    }
    
    std::string getName() const override { return "Makespan"; }
    bool minimize() const override { return true; }
};

class ObjectiveManager {
private:
    std::vector<std::shared_ptr<IObjective>> objectives;
    
public:
    void addObjective(std::shared_ptr<IObjective> obj) {
        objectives.push_back(obj);
    }
    
    std::vector<double> evaluateAll(const Solution& solution) const {
        std::vector<double> values;
        values.reserve(objectives.size());
        for (const auto& obj : objectives) {
            values.push_back(obj->evaluate(solution));
        }
        return values;
    }
    
    size_t getObjectiveCount() const { return objectives.size(); }
    
    const IObjective* getObjective(size_t index) const {
        return index < objectives.size() ? objectives[index].get() : nullptr;
    }
};
