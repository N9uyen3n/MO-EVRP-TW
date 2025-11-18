// src/alns/operators/repair/RegretKRepair.cpp
#include "alns/operators/repair/RegretKRepair.h"
#include "core/Vehicle.h"
#include "core/Station.h"   // <-- SỬA: Thêm vào
#include "core/Customer.h"  // <-- SỬA: Thêm vào
#include "core/Depot.h"     // <-- SỬA: Thêm vào
#include <algorithm>
#include <vector>
#include <iostream>
#include <map>
#include <tuple>
#include <utility>         // <-- SỬA: Thêm vào cho std::move

RegretKRepair::RegretKRepair(std::shared_ptr<Instance> instance, int k, double noiseParameter)
    : instance(std::move(instance)), k_regret(k), noiseParam(noiseParameter) {} // <-- SỬA: Dùng std::move

std::string RegretKRepair::getName() const {
    return "Regret-k Repair";
}

const double W_DIST = 0.7;
const double W_CHARGE = 0.3;

std::vector<RegretKRepair::InsertionCost>
RegretKRepair::findKBestInsertions(int customerId, Solution& solution, std::mt19937& rng) {

    std::vector<InsertionCost> allInsertions;

    for (size_t r_idx = 0; r_idx < solution.getRoutes().size(); ++r_idx) {
        const Route& route = solution.getRoutes()[r_idx];

        for (size_t pos = 1; pos < route.getNodes().size(); ++pos) {

            InsertionResult result = route.checkInsertionCost(customerId, pos);

            if (result.isFeasible) {
                double deltaCost = W_DIST * result.deltaDistance +
                                   W_CHARGE * result.deltaChargeAmount;

                // Thêm nhiễu (noise)
                double noiseFactor = (std::uniform_real_distribution<>(0.0, 1.0)(rng) * noiseParam);
                deltaCost = deltaCost * (1.0 + noiseFactor);

                allInsertions.push_back({(int)r_idx, pos, deltaCost, true});
            }
        }
    }

    // Sắp xếp các vị trí chèn theo chi phí
    std::sort(allInsertions.begin(), allInsertions.end(),
              [](const InsertionCost& a, const InsertionCost& b) {
        return a.cost < b.cost;
    });
    return allInsertions;
}


void RegretKRepair::execute(Solution& solution, const std::vector<int>& unservedCustomers, std::mt19937& rng) {

    std::vector<int> unserved = unservedCustomers;

    // <-- SỬA: Lấy danh sách trạm sạc 1 lần (để dùng khi tạo tuyến mới)
    std::vector<int> stationIds;
    for (const auto& node : instance->getNodes()) {
        if (std::dynamic_pointer_cast<Station>(node) && node->getId() > 1) {
            stationIds.push_back(node->getId());
        }
    }

    while (!unserved.empty()) {

        std::vector<std::tuple<double, int, InsertionCost>> regretList;
        regretList.reserve(unserved.size()); // <-- SỬA: Thêm reserve

        for (size_t i = 0; i < unserved.size(); ++i) {
            int customerId = unserved[i];
            std::vector<InsertionCost> k_best = findKBestInsertions(customerId, solution, rng);

            if (k_best.empty()) continue; // Không chèn được vào đâu

            // Tính toán "Regret" (hối tiếc)
            double regret = 0.0;
            int k_val = std::min((int)k_best.size(), k_regret);
            if (k_val > 1) {
                for(int j = 1; j < k_val; ++j) {
                    // Chi phí hối tiếc là tổng chênh lệch giữa
                    // vị trí tốt nhất và các vị trí tốt tiếp theo
                    regret += (k_best[j].cost - k_best[0].cost);
                }
            }
            regretList.push_back({regret, customerId, k_best[0]});
        }

        // ==========================================================
        // *** SỬA LỖI LOGIC: NÂNG CẤP PHẦN TẠO TUYẾN MỚI ***
        // ==========================================================
        if (regretList.empty()) {
            // Tình huống: Không khách hàng nào có thể chèn vào tuyến hiện có.
            // Ta phải tạo tuyến mới.
            // Logic cũ (tạo tuyến mới cho MỌI khách hàng) là RẤT TỆ.
            // Logic MỚI: Chỉ tạo 1 tuyến mới cho 1 khách hàng (đầu tiên),
            // sử dụng logic chèn trạm sạc đầy đủ (giống GreedyRepair).

            if (unserved.empty()) break; // (Kiểm tra an toàn)

            int customerId = unserved[0]; // Lấy khách hàng đầu tiên
            bool addedSuccessfully = false;

            // Tạo xe và tuyến mới
            auto vehicle = std::make_shared<Vehicle>(
                static_cast<int>(solution.getNumRoutes() + 1),
                instance->getVehicleCapacity(),
                instance->getVehicleBattery(),
                instance->getVehicleEnergyRate()
            );
            Route newRoute(static_cast<int>(solution.getNumRoutes() + 1), vehicle, instance);

            // --- BƯỚC A: Thử tuyến đơn giản D -> C -> D ---
            newRoute.addNode(customerId, 1);

            if (newRoute.isFeasible()) {
                solution.addRoute(newRoute);
                addedSuccessfully = true;
            } else {
                // --- BƯỚC B: Thất bại, thử chèn trạm sạc ---
                for (int stationId : stationIds) {
                    // Thử kịch bản D -> S -> C -> D
                    newRoute.clear(); // Reset về D -> D
                    newRoute.addNode(stationId, 1);
                    newRoute.addNode(customerId, 2);
                    if (newRoute.isFeasible()) {
                        solution.addRoute(newRoute);
                        addedSuccessfully = true;
                        break; // Thoát vòng lặp trạm sạc
                    }

                    // Thử kịch bản D -> C -> S -> D
                    newRoute.clear(); // Reset về D -> D
                    newRoute.addNode(customerId, 1);
                    newRoute.addNode(stationId, 2);
                    if (newRoute.isFeasible()) {
                        solution.addRoute(newRoute);
                        addedSuccessfully = true;
                        break; // Thoát vòng lặp trạm sạc
                    }
                }
            }

            // --- BƯỚC C: Xử lý kết quả ---
            if (!addedSuccessfully) {
                // Thất bại hoàn toàn, báo lỗi
                std::cerr << "WARNING: Customers " << customerId << " Cannot insert (RegretKRepair fallback)!" << std::endl;
            }

            // Xóa khách hàng này khỏi danh sách (dù thành công hay thất bại)
            // để tránh vòng lặp vô hạn.
            unserved.erase(unserved.begin());

            // Bỏ qua phần còn lại của vòng lặp và bắt đầu lại
            // (tính toán lại regret cho các khách hàng còn lại với tuyến mới)
            continue;
        }

        // Sắp xếp: Ưu tiên khách hàng có "hối tiếc" (regret) cao nhất
        auto compareRegret = [](
            const std::tuple<double, int, InsertionCost>& a,
            const std::tuple<double, int, InsertionCost>& b)
        {
            return std::get<0>(a) > std::get<0>(b); // (Sắp xếp giảm dần)
        };
        std::sort(regretList.begin(), regretList.end(), compareRegret);

        // Lấy khách hàng hối tiếc nhất và chèn vào vị trí tốt nhất của họ
        auto [regret, cust_to_insert, best_insert] = regretList[0];
        solution.getRoutes()[best_insert.routeIndex].addNode(
            cust_to_insert, best_insert.position
        );

        // Xóa khách hàng đã được chèn khỏi danh sách
        unserved.erase(std::remove(unserved.begin(), unserved.end(), cust_to_insert), unserved.end());
    }
    solution.evaluateRoutes();
}