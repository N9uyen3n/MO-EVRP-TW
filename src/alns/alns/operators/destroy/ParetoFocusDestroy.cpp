#include "../../../../include/alns/operators/destroy/ParetoFocusDestroy.h"
#include "../../../../include/core/Solution.h"
#include "../../../../include/core/Customer.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>

ParetoFocusDestroy::ParetoFocusDestroy(std::shared_ptr<Instance> inst, int p)
    : instance(inst), p_param(p) {}

std::string ParetoFocusDestroy::getName() const {
    return "Pareto Focus Destroy";
}

std::vector<int> ParetoFocusDestroy::execute(Solution& solution, int nodesToRemove, std::mt19937& rng) {
    std::vector<int> removedCustomers;
    auto& routes = solution.getRoutes();
    if (routes.empty()) {
        return removedCustomers;
    }

    // 1. Chọn mục tiêu ngẫu nhiên (0: Dist, 1: Time, 2: Workload)
    std::uniform_int_distribution<> distObj(0, 2);
    int focusObj = distObj(rng);

    // Tính toán trước các giá trị cần thiết cho mục tiêu Workload
    double meanRouteDuration = 0.0;
    if (focusObj == 2) {
        if (!routes.empty()) {
            double totalDuration = 0;
            for(const auto& route : routes) {
                totalDuration += route.getTotalTime();
            }
            meanRouteDuration = totalDuration / routes.size();
        }
    }

    struct Candidate {
        int nodeId;
        double cost; // "cost" ở đây là "badness score", càng cao càng tệ
        bool operator>(const Candidate& other) const { return cost > other.cost; }
    };
    std::vector<Candidate> candidates;

    for (const auto& route : routes) {
        const auto& nodes = route.getNodes();
        if (nodes.size() <= 2) continue;

        for (size_t i = 1; i < nodes.size() - 1; ++i) {
            int curr = nodes[i];

            auto nodePtr = instance->getNodeById(curr);
            if (!std::dynamic_pointer_cast<Customer>(nodePtr)) {
                continue;
            }

            double cost = 0.0;

            if (focusObj == 0) { // DISTANCE: Chi phí là phần distance "tiết kiệm" được nếu xóa node
                int prev = nodes[i-1];
                int next = nodes[i+1];
                cost = instance->getDistance(prev, curr) + instance->getDistance(curr, next)
                       - instance->getDistance(prev, next);
            }
            else if (focusObj == 1) { // TIME: Tương tự, chi phí là thời gian di chuyển tiết kiệm được
                int prev = nodes[i-1];
                int next = nodes[i+1];
                cost = instance->getTime(prev, curr) + instance->getTime(curr, next)
                       - instance->getTime(prev, next);
            }
            else { // WORKLOAD: Chi phí là độ lệch của tuyến chứa node so với trung bình
                cost = std::abs(route.getTotalTime() - meanRouteDuration);
            }
            candidates.push_back({curr, cost});
        }
    }

    // Sort giảm dần (Node "tệ" nhất sẽ có cost cao nhất)
    std::sort(candidates.begin(), candidates.end(), std::greater<Candidate>());

    // Chọn xóa (Randomized Greedy - Shaw selection)
    while (removedCustomers.size() < nodesToRemove && !candidates.empty()) {
        std::uniform_real_distribution<double> rnd(0.0, 1.0);
        int idx = static_cast<int>(candidates.size() * std::pow(rnd(rng), p_param));
        idx = std::min(idx, static_cast<int>(candidates.size() - 1));

        // Tránh xóa trùng lặp
        bool alreadyRemoved = false;
        for (int removedId : removedCustomers) {
            if (candidates[idx].nodeId == removedId) {
                alreadyRemoved = true;
                break;
            }
        }
        if (!alreadyRemoved) {
            removedCustomers.push_back(candidates[idx].nodeId);
        }
        candidates.erase(candidates.begin() + idx);
    }

    // Rebuild routes (Tạo lại tuyến không có các node bị xóa)
    for (auto& route : routes) {
        std::vector<int> newSeq;
        bool changed = false;
        const auto& oldNodes = route.getNodes();

        newSeq.push_back(oldNodes[0]); // Giữ Depot đầu
        for (size_t i=1; i < oldNodes.size()-1; ++i) {
            bool del = false;
            for(int id : removedCustomers) {
                if(id == oldNodes[i]) {
                    del = true;
                    break;
                }
            }

            if(!del) newSeq.push_back(oldNodes[i]);
            else changed = true;
        }
        newSeq.push_back(oldNodes.back()); // Giữ Depot cuối

        if (changed) {
            route.clear();
            for(size_t i=1; i < newSeq.size()-1; ++i) route.addNode(newSeq[i], i);
            route.evaluate();
        }
    }

    return removedCustomers;
}
