#pragma once
#include "alns/IOperator.h"
#include "core/Instance.h"
#include <memory>
#include <string>

class GreedyDistanceInsertion : public IRepairOperator {
private:
    std::shared_ptr<Instance> instance;

public:
    explicit GreedyDistanceInsertion(std::shared_ptr<Instance> inst);

    std::string getName() const override;

    void execute(Solution& solution, const std::vector<int>& unservedCustomers, std::mt19937& rng) override;
};