#pragma once
#include "../../IOperator.h"
#include "../../../core/Instance.h"
#include "../../../core/Solution.h"
#include <limits>

/**
 * @brief Giống GreedyRepair, nhưng có khả năng chèn trạm sạc
 * một cách thông minh nếu việc chèn khách hàng bị lỗi năng lượng.
 */
class GreedyStationRepair : public IRepairOperator {
public:
    explicit GreedyStationRepair(std::shared_ptr<Instance> instance);

    std::string getName() const override;

    void execute(Solution& solution, const std::vector<int>& unservedCustomers, std::mt19937& rng) override;

private:
    std::shared_ptr<Instance> instance;

    struct Insertion {
        int customerId = -1;
        int routeIndex = -1;
        size_t position = 0;
        double cost = std::numeric_limits<double>::infinity();
        bool isFeasible = false;
        
        // Thông tin nếu cần chèn trạm sạc
        bool requiresStation = false;
        int stationId = -1;
        size_t stationPosition = 0;
    };

    /**
     * @brief Tìm vị trí chèn tốt nhất, có xét đến việc chèn trạm sạc.
     */
    Insertion findBestGreedyStationInsertion(int customerId, Solution& solution);
};