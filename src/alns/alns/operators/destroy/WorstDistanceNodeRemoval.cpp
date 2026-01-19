#include "../../../../include/alns/operators/destroy/WorstDistanceNodeRemoval.h"
#include "../../../../include/core/Customer.h" // Cần để kiểm tra dynamic_cast
#include <algorithm>
#include <cmath>
#include <vector>

WorstDistanceNodeRemoval::WorstDistanceNodeRemoval(std::shared_ptr<Instance> instance, int determinism_param)
    : instance(instance), determinism(determinism_param) {}

std::string WorstDistanceNodeRemoval::getName() const {
    return "Worst Distance Node Removal";
}

double WorstDistanceNodeRemoval::calculateRemovalCost(const Route& route, size_t position) {
    const auto& nodes = route.getNodes();
    // Đảm bảo vị trí hợp lệ (không phải đầu hoặc cuối)
    if (position <= 0 || position >= nodes.size() - 1) return 0.0;

    int prev = nodes[position - 1];
    int curr = nodes[position];
    int next = nodes[position + 1];

    // Chi phí hiện tại (có node)
    double distWith = instance->getDistance(prev, curr) + instance->getDistance(curr, next);
    
    // Chi phí nếu bỏ node (nối trực tiếp prev -> next)
    double distWithout = instance->getDistance(prev, next);

    // Lượng tiết kiệm được (Distance Saving) = Chi phí xóa
    // Càng lớn nghĩa là node này gây đường vòng càng xa -> Càng "Tệ"
    return distWith - distWithout;
}

std::vector<int> WorstDistanceNodeRemoval::execute(Solution& solution, int nodesToRemove, std::mt19937& rng) {
    std::vector<int> removedCustomers;
    auto& routes = solution.getRoutes();

    // Cấu trúc để lưu các ứng viên
    struct Candidate {
        int nodeId;
        double cost; // Lượng distance tiết kiệm được nếu xóa
        // Sắp xếp giảm dần (Node tệ nhất/tiết kiệm nhiều nhất lên đầu)
        bool operator>(const Candidate& other) const { return cost > other.cost; }
    };
    std::vector<Candidate> candidates;

    // 1. TÍNH CHI PHÍ CHO TẤT CẢ KHÁCH HÀNG
    for (const auto& route : routes) {
        const auto& nodes = route.getNodes();
        // Bỏ qua tuyến rỗng hoặc chỉ có depot
        if (nodes.size() <= 2) continue;

        // Duyệt các node ở giữa (bỏ 2 depot đầu cuối)
        for (size_t i = 1; i < nodes.size() - 1; ++i) {
            int nodeId = nodes[i];

            // Chỉ xem xét Customer (không xóa Station ở toán tử này)
            if (std::dynamic_pointer_cast<Customer>(instance->getNodeById(nodeId))) {
                double cost = calculateRemovalCost(route, i);
                candidates.push_back({nodeId, cost});
            }
        }
    }

    // 2. SẮP XẾP GIẢM DẦN
    std::sort(candidates.begin(), candidates.end(), std::greater<Candidate>());

    // 3. CHỌN XÓA (Randomized Greedy Selection)
    // Thay vì luôn chọn top k, ta chọn ngẫu nhiên dựa trên tham số determinism (p)
    while (removedCustomers.size() < nodesToRemove && !candidates.empty()) {
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        double r = dist(rng);

        // Công thức chọn index: idx = |List| * r^p
        // p càng lớn -> idx càng gần 0 (chọn phần tử tốt nhất)
        // p = 1 -> Random đều
        int idx = static_cast<int>(candidates.size() * std::pow(r, determinism));
        
        // Đảm bảo index hợp lệ
        if (idx >= candidates.size()) idx = candidates.size() - 1;

        removedCustomers.push_back(candidates[idx].nodeId);
        candidates.erase(candidates.begin() + idx);
    }

    // 4. CẬP NHẬT SOLUTION (Xóa node khỏi route)
    for (auto& route : routes) {
        std::vector<int> newSequence;
        bool changed = false;
        const auto& oldNodes = route.getNodes();

        newSequence.push_back(oldNodes[0]); // Giữ depot đầu

        for (size_t i = 1; i < oldNodes.size() - 1; ++i) {
            int nid = oldNodes[i];
            bool isRemoved = false;
            
            // Kiểm tra xem node này có nằm trong danh sách xóa không
            for (int remId : removedCustomers) {
                if (nid == remId) {
                    isRemoved = true;
                    break;
                }
            }

            if (!isRemoved) {
                newSequence.push_back(nid);
            } else {
                changed = true;
            }
        }
        newSequence.push_back(oldNodes.back()); // Giữ depot cuối

        // Nếu tuyến có thay đổi, cập nhật lại
        if (changed) {
            route.clear();
            // Add lại các node vào tuyến
            for (size_t i = 1; i < newSequence.size() - 1; ++i) {
                route.addNode(newSequence[i], i);
            }
            route.evaluate(); // Tính toán lại chi phí tuyến
        }
    }

    return removedCustomers;
}