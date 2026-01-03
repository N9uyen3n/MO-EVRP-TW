#include "../../../../include/alns/operators/repair/RegretKRepair.h"
#include <algorithm>
#include <vector>
#include <cmath>

RegretKRepair::RegretKRepair(std::shared_ptr<Instance> instance, int k, double noiseParameter)
    : instance(instance), k_regret(k), noiseParam(noiseParameter) {}

std::string RegretKRepair::getName() const {
    return "Regret-" + std::to_string(k_regret) + " Repair";
}

void RegretKRepair::execute(Solution& solution, const std::vector<int>& unservedCustomers, std::mt19937& rng) {
    std::vector<int> remainingCustomers = unservedCustomers;

    // Lặp cho đến khi chèn hết khách hàng
    while (!remainingCustomers.empty()) {
        int bestCustId = -1;
        int bestRouteIdx = -1;
        int bestPos = -1;
        double maxRegret = -1.0;

        // Tìm Regret cho từng khách hàng
        for (int custId : remainingCustomers) {
            // Tìm k vị trí chèn tốt nhất cho khách hàng này
            std::vector<InsertionCost> kBest = findKBestInsertions(custId, solution, rng);

            if (kBest.empty()) continue; // Không chèn được vào đâu

            // Tính giá trị Regret
            // Regret = Cost(Best_k) - Cost(Best_1)
            // (Hoặc tổng hiệu số tùy biến thể. Ở đây dùng Regret-2 cơ bản: 2nd - 1st)
            double regretVal = 0.0;
            double bestCost = kBest[0].cost;

            if (kBest.size() >= k_regret) {
                regretVal = kBest[k_regret - 1].cost - bestCost;
            } else if (kBest.size() > 1) {
                // Nếu không đủ k vị trí, lấy vị trí tệ nhất tìm được trừ vị trí tốt nhất
                regretVal = kBest.back().cost - bestCost;
            } else {
                // Chỉ có đúng 1 chỗ để chèn -> Regret rất lớn (cần ưu tiên chèn ngay)
                regretVal = std::numeric_limits<double>::max(); 
            }

            if (regretVal > maxRegret) {
                maxRegret = regretVal;
                bestCustId = custId;
                bestRouteIdx = kBest[0].routeIndex;
                bestPos = kBest[0].position;
            }
        }

        // Thực hiện chèn khách hàng có Regret lớn nhất
        if (bestCustId != -1) {
            solution.getRoutes()[bestRouteIdx].addNode(bestCustId, bestPos);
            solution.getRoutes()[bestRouteIdx].evaluate();

            // Xóa khách hàng này khỏi danh sách remaining
            remainingCustomers.erase(std::remove(remainingCustomers.begin(), remainingCustomers.end(), bestCustId), remainingCustomers.end());
        } else {
            // Không chèn được khách nào nữa (Infeasible toàn tập)
            // Break để tránh lặp vô tận
            break; 
        }
    }
}

std::vector<RegretKRepair::InsertionCost> RegretKRepair::findKBestInsertions(int customerId, Solution& solution, std::mt19937& rng) {
    std::vector<InsertionCost> allInsertions;
    auto& routes = solution.getRoutes();

    for (int r = 0; r < routes.size(); ++r) {
        const auto& nodes = routes[r].getNodes();
        for (size_t j = 0; j < nodes.size() - 1; ++j) {
            InsertionResult res = routes[r].checkInsertionCost(customerId, j + 1);
            if (res.isFeasible) {
                // Hàm mục tiêu tổng quát cho Regret (thường là Distance)
                double baseCost = res.deltaDistance; 

                // Thêm Nhiễu (Noise) để đa dạng hóa
                // Cost' = Cost * (1 + random(-noise, noise))
                if (noiseParam > 0) {
                    std::uniform_real_distribution<double> dist(-noiseParam, noiseParam);
                    baseCost *= (1.0 + dist(rng));
                    if (baseCost < 0) baseCost = 0; // An toàn
                }

                allInsertions.push_back({r, j + 1, baseCost, true});
            }
        }
    }

    // Sắp xếp tăng dần theo cost
    std::sort(allInsertions.begin(), allInsertions.end(), [](const InsertionCost& a, const InsertionCost& b) {
        return a.cost < b.cost;
    });

    // Chỉ lấy top k
    if (allInsertions.size() > k_regret) {
        allInsertions.resize(k_regret);
    }

    return allInsertions;
}