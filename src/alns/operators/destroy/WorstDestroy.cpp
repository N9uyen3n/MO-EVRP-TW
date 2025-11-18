#include "alns/operators/destroy/WorstDestroy.h"
#include <algorithm>
#include <vector>
#include <cmath>
#include <iostream>

WorstDestroy::WorstDestroy(std::shared_ptr<Instance> instance, int determinism_param)
    : instance(instance), determinism(determinism_param) {
}

std::string WorstDestroy::getName() const {
    return "Worst Destroy";
}

double WorstDestroy::calculateRemovalCost(const Route& route, size_t position) {
    const auto& nodes = route.getNodes();
    
    // Chỉ tính cho customer, bỏ qua depot/trạm sạc
    if (position == 0 || position >= nodes.size() - 1 || 
        instance->getNodeById(nodes[position])->getDemand() == 0) {
        return -1e9; // Trả về chi phí rất nhỏ
    }

    int prevNode = nodes[position - 1];
    int currNode = nodes[position];
    int nextNode = nodes[position + 1];

    double dist_before = instance->getDistance(prevNode, currNode) + 
                         instance->getDistance(currNode, nextNode);
    double dist_after = instance->getDistance(prevNode, nextNode);
    
    // (Bạn có thể thêm deltaWaitTime, deltaEnergy... vào đây)
    
    // Cost contribution, càng LỚN càng "tệ"
    return dist_before - dist_after;
}

std::vector<int> WorstDestroy::execute(Solution& solution, int nodesToRemove, std::mt19937& rng) {
    
    std::vector<int> removedCustomers;
    std::set<int> customersToRemoveSet; // Dùng set để tránh xóa 2 lần

    for (int k = 0; k < nodesToRemove; ++k) {
        
        // <cost, {routeIdx, posIdx, customerId}>
        std::vector<std::tuple<double, int, int, int>> costList;

        for (size_t r_idx = 0; r_idx < solution.getRoutes().size(); ++r_idx) {
            const auto& route = solution.getRoutes()[r_idx];
            for (size_t p_idx = 1; p_idx < route.getNodes().size() - 1; ++p_idx) {
                int cust_id = route.getNodes()[p_idx];
                
                // Bỏ qua nếu đã chọn xóa
                if (customersToRemoveSet.count(cust_id)) continue; 
                
                double cost = calculateRemovalCost(route, p_idx);
                if (cost > -1e8) { // Bỏ qua depot/trạm sạc
                    costList.push_back({cost, (int)r_idx, (int)p_idx, cust_id});
                }
            }
        }

        if (costList.empty()) {
            break; // Không còn gì để xóa
        }

        // Sắp xếp: "Tệ" nhất (cost cao nhất) lên đầu
        std::sort(costList.rbegin(), costList.rend());

        double r = std::uniform_real_distribution<>(0.0, 1.0)(rng);
        int idx = (int)(std::pow(r, determinism) * costList.size());
        idx = std::min(idx, (int)costList.size() - 1); 

        // Lấy khách hàng tệ nhất
        int cust_id_to_remove = std::get<3>(costList[idx]);
        customersToRemoveSet.insert(cust_id_to_remove);
        removedCustomers.push_back(cust_id_to_remove);
    }

    // Thực hiện XÓA (sau khi đã chọn xong)
    for (auto& route : solution.getRoutes()) {
        auto& nodes = route.getNodes();
        size_t i = 1;
        while (i < nodes.size() - 1) {
            if (customersToRemoveSet.count(nodes[i])) {
                route.removeNode(i);
            } else {
                i++;
            }
        }
    }
    
    solution.evaluateRoutes();
    return removedCustomers;
}