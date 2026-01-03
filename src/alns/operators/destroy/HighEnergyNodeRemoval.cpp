#include "../../../../include/alns/operators/destroy/HighEnergyNodeRemoval.h"
#include <algorithm>
#include <cmath>

HighEnergyNodeRemoval::HighEnergyNodeRemoval(std::shared_ptr<Instance> instance, int p)
    : instance(instance), p_param(p) {}

std::string HighEnergyNodeRemoval::getName() const {
    return "High Energy Node Removal";
}

std::vector<int> HighEnergyNodeRemoval::execute(Solution& solution, int nodesToRemove, std::mt19937& rng) {
    std::vector<int> removedCustomers;
    auto& routes = solution.getRoutes();
    
    struct Candidate {
        int nodeId;
        double savedEnergy;
        bool operator>(const Candidate& other) const { return savedEnergy > other.savedEnergy; }
    };
    std::vector<Candidate> candidates;

    // 1. Tính toán lượng năng lượng tiết kiệm được nếu xóa node
    for (const auto& route : routes) {
        const auto& nodes = route.getNodes();
        if (nodes.size() <= 2) continue;

        for (size_t i = 1; i < nodes.size() - 1; ++i) {
            int curr = nodes[i];
            // Bỏ qua nếu là Station (để toán tử khác lo)
            if (std::dynamic_pointer_cast<Customer>(instance->getNodeById(curr)) == nullptr) continue;

            int prev = nodes[i-1];
            int next = nodes[i+1];

            // Tiết kiệm = (E_prev_curr + E_curr_next) - E_prev_next
            // Năng lượng tỉ lệ thuận với khoảng cách (nhân với consumptionRate)
            double rate = instance->getVehicleEnergyRate(); // Giả định có hàm này
            double distWith = instance->getDistance(prev, curr) + instance->getDistance(curr, next);
            double distWithout = instance->getDistance(prev, next);
            
            double saved = (distWith - distWithout) * rate;
            candidates.push_back({curr, saved});
        }
    }

    // 2. Sắp xếp giảm dần (Tiết kiệm nhiều năng lượng nhất lên đầu)
    std::sort(candidates.begin(), candidates.end(), std::greater<Candidate>());

    // 3. Chọn ngẫu nhiên có định hướng (Randomized Greedy)
    while (removedCustomers.size() < nodesToRemove && !candidates.empty()) {
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        double r = dist(rng);
        // Index = size * r^p
        int idx = static_cast<int>(candidates.size() * std::pow(r, p_param));
        if (idx >= candidates.size()) idx = candidates.size() - 1;

        removedCustomers.push_back(candidates[idx].nodeId);
        candidates.erase(candidates.begin() + idx);
    }

    // 4. Xóa khỏi tuyến (Rebuild routes)
    for (auto& route : routes) {
        std::vector<int> newSeq;
        bool changed = false;
        const auto& oldNodes = route.getNodes();
        
        newSeq.push_back(oldNodes[0]);
        for (size_t i=1; i < oldNodes.size()-1; ++i) {
            bool del = false;
            for(int remId : removedCustomers) if(remId == oldNodes[i]) { del=true; break; }
            if(!del) newSeq.push_back(oldNodes[i]);
            else changed = true;
        }
        newSeq.push_back(oldNodes.back());

        if (changed) {
            route.clear();
            for(size_t i=1; i<newSeq.size()-1; ++i) route.addNode(newSeq[i], i);
            route.evaluate();
        }
    }

    return removedCustomers;
}