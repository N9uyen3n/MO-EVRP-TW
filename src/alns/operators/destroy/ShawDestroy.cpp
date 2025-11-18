// src/alns/operators/destroy/ShawDestroy.cpp

#include "alns/operators/destroy/ShawDestroy.h"
#include "core/Customer.h" // Cần để dynamic_pointer_cast
#include <algorithm>
#include <cmath>     // <-- SỬA: Thêm include cho std::abs
#include <set>       // <-- SỬA: Thêm include cho std::set (dùng ở cuối)
#include <map>

ShawDestroy::ShawDestroy(std::shared_ptr<Instance> instance,
                         RelatednessWeights weights, int determinism_param)
    : instance(instance), weights(weights), determinism(determinism_param) {

    // GHI CHÚ: Bạn cần đảm bảo class Instance có các hàm
    // getMaxDistance(), getMaxTimeWindow(), getMaxDemand()
    // Nếu không, bạn phải tính toán chúng một lần ở đây (trong constructor)
    // và lưu trữ lại để dùng cho việc chuẩn hóa.
}

std::string ShawDestroy::getName() const {
    return "Shaw Destroy";
}

/**
 * @brief (HÀM ĐÃ SỬA) Tính toán độ liên quan, ĐÃ CHUẨN HÓA
 */
double ShawDestroy::calculateRelatedness(int cust1_id, int cust2_id,
                                           const std::map<int, int>& custToRoute,
                                           const std::map<int, double>& custToDemand) {

    if (cust1_id == cust2_id) return 0.0;

    // Lấy node (Giả định ID là hợp lệ)
    auto node1 = std::dynamic_pointer_cast<Customer>(instance->getNodeById(cust1_id));
    auto node2 = std::dynamic_pointer_cast<Customer>(instance->getNodeById(cust2_id));

    if (!node1 || !node2) {
        // (Nếu 1 trong 2 không phải Customer, trả về độ liên quan thấp)
        return 1e9;
    }

    // --- SỬA LỖI LOGIC: CHUẨN HÓA (NORMALIZATION) ---

    // 1. Khoảng cách (Distance)
    double dist = instance->getDistance(cust1_id, cust2_id);
    // (Giả sử instance->getMaxDistance() trả về khoảng cách lớn nhất trong bài toán)
    double normDist = dist / instance->getMaxDistance();

    // 2. Cửa sổ thời gian (Time Window)
    double time = std::abs(node1->getReadyTime() - node2->getReadyTime()) +
                  std::abs(node1->getDueDate() - node2->getDueDate());
    double normTime = time / instance->getMaxTimeWindow();

    // 3. Nhu cầu (Demand) - Lấy từ map cache
    double demand = std::abs(custToDemand.at(cust1_id) - custToDemand.at(cust2_id));
    double normDemand = demand / instance->getMaxDemand();

    // 4. Chung tuyến (Route) - Đã chuẩn hóa (0 hoặc 1)
    double route = (custToRoute.at(cust1_id) == custToRoute.at(cust2_id)) ? 0.0 : 1.0;

    // Tính toán cuối cùng với các giá trị ĐÃ CHUẨN HÓA
    return weights.distanceWeight * normDist +
           weights.timeWindowWeight * normTime +
           weights.demandWeight * normDemand +
           weights.routeWeight * route;
}


std::vector<int> ShawDestroy::execute(Solution& solution, int nodesToRemove, std::mt19937& rng) {

    std::vector<int> allCustomers;
    std::map<int, int> custToRoute;
    std::map<int, double> custToDemand;

    // <-- SỬA: Thêm .reserve()
    // (Giả sử instance có hàm trả về list customers)
    allCustomers.reserve(instance->getCustomers().size());

    // Thu thập thông tin khách hàng (phục vụ cho việc cache)
    for (size_t r_idx = 0; r_idx < solution.getRoutes().size(); ++r_idx) {
        const auto& nodes = solution.getRoutes()[r_idx].getNodes();
        for (size_t p_idx = 1; p_idx < nodes.size() - 1; ++p_idx) {
            int cust_id = nodes[p_idx];
            // (Nên dùng instance->isCustomer(cust_id) thay vì > 0)
            if (cust_id > 0) {
                allCustomers.push_back(cust_id);
                custToRoute[cust_id] = r_idx;
                // (Nên lấy demand từ instance và cache ở đây)
                custToDemand[cust_id] = instance->getNodeById(cust_id)->getDemand();
            }
        }
    }

    if (allCustomers.empty()) {
        return {};
    }

    // Chọn ngẫu nhiên 1 khách hàng làm "hạt giống"
    std::uniform_int_distribution<> dist(0, allCustomers.size() - 1);
    int baseCustomerIdx = dist(rng);
    int baseCustomer = allCustomers[baseCustomerIdx];
    allCustomers.erase(allCustomers.begin() + baseCustomerIdx); // Xóa khỏi danh sách

    std::vector<int> removedCustomers;
    std::set<int> customersToRemoveSet;

    removedCustomers.reserve(nodesToRemove); // <-- SỬA: Thêm .reserve()

    removedCustomers.push_back(baseCustomer);
    customersToRemoveSet.insert(baseCustomer);

    // Bắt đầu vòng lặp xóa
    while (removedCustomers.size() < (size_t)nodesToRemove && !allCustomers.empty()) {

        std::vector<std::pair<double, int>> relatednessList;
        relatednessList.reserve(allCustomers.size()); // <-- SỬA: Thêm .reserve()

        // Tính độ liên quan với khách hàng vừa bị xóa (baseCustomer)
        for (int otherCustomer : allCustomers) {
            double R = calculateRelatedness(baseCustomer, otherCustomer, custToRoute, custToDemand);
            relatednessList.push_back({R, otherCustomer});
        }

        if (relatednessList.empty()) break;

        // Sắp xếp (liên quan nhất lên đầu)
        std::sort(relatednessList.begin(), relatednessList.end());

        // Chọn (dùng tham số determinism)
        double r = std::uniform_real_distribution<>(0.0, 1.0)(rng);
        int idx = (int)(std::pow(r, determinism) * relatednessList.size());
        idx = std::min(idx, (int)relatednessList.size() - 1);

        int cust_to_remove = relatednessList[idx].second;

        // Thêm vào danh sách xóa
        removedCustomers.push_back(cust_to_remove);
        customersToRemoveSet.insert(cust_to_remove);

        // Cập nhật baseCustomer cho vòng lặp tiếp theo
        baseCustomer = cust_to_remove;

        // Xóa khỏi allCustomers (để không bị chọn lại)
        allCustomers.erase(
            std::remove(allCustomers.begin(), allCustomers.end(), cust_to_remove),
            allCustomers.end()
        );
    }

    // Thực hiện xóa (sau khi đã chọn xong)
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