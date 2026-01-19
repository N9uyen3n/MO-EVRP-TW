#pragma once
#include "alns/IOperator.h"
#include "core/Instance.h" // Sửa từ ../../core/Instance.h thành core/Instance.h
#include <vector>
#include <map>
#include <string>
#include <random>
#include <memory>

class ShawDestroy : public IDestroyOperator {
public:
    explicit ShawDestroy(std::shared_ptr<Instance> instance, int determinism_param = 6);
    std::string getName() const override;
    std::vector<int> execute(Solution& solution, int nodesToRemove, std::mt19937& rng) override;

private:
    std::shared_ptr<Instance> instance;
    int determinism;
    double calculateRelatedness(int cust1_id, int cust2_id,
                                const std::map<int, int>& custToRoute,
                                const std::map<int, double>& custToDemand,
                                double w_dist, double w_time, double w_demand, double w_route);
};