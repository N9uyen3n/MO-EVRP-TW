#include "Route.h"
#include "Instance.h"
#include "Customer.h"
#include "Station.h"
#include "Depot.h"
#include <stdexcept>
#include <algorithm> // Cho std::max
#include <cmath>     // Cho std::abs (kiểm tra số thực)
#include <iostream>
#include <ostream>

/**
 * @brief Khởi tạo một tuyến đường mới, ban đầu chỉ chứa depot (bắt đầu và kết thúc).
 */
Route::Route(int id, std::shared_ptr<Vehicle> vehicle, const std::shared_ptr<Instance>& instance)
    : id(id), vehicle(vehicle), instance(instance), evalResult() {

    // Một tuyến đường luôn bắt đầu và kết thúc tại depot (ID = 0)
    nodeSequence.push_back(0);
    nodeSequence.push_back(0);

    // Đánh giá tuyến đường D->D ban đầu
    evaluate();
}

/**
 * @brief Thêm một node vào vị trí 'position'.
 * Vị trí 1 là ngay sau depot bắt đầu.
 */
void Route::addNode(int nodeId, size_t position) {
    if (position < 1 || position > nodeSequence.size() - 1) {
        throw std::out_of_range("Vi tri them node khong hop le. Phai nam giua 2 depot.");
    }
    // Chèn vào *trước* vị trí 'position'
    nodeSequence.insert(nodeSequence.begin() + position, nodeId);
    evaluate(); // Đánh giá lại tuyến đường
}

/**
 * @brief Xóa một node tại vị trí 'position'.
 * Vị trí 1 là node đầu tiên sau depot.
 */
void Route::removeNode(size_t position) {
    if (position < 1 || position > nodeSequence.size() - 2) {
        throw std::out_of_range("Vi tri xoa node khong hợp lệ. Khong the xoa depot.");
    }
    nodeSequence.erase(nodeSequence.begin() + position);
    evaluate(); // Đánh giá lại tuyến đường
}

/**
 * @brief Xóa toàn bộ khách hàng và trạm sạc, chỉ để lại D->D
 */
void Route::clear() {
    nodeSequence.clear();
    nodeSequence.push_back(0);
    nodeSequence.push_back(0);
    evaluate();
}

/**
 * @brief Hàm "trái tim" của Route.
 * Tính toán tất cả chi phí và kiểm tra tính khả thi.
 * Thực thi logic EVRPTW-PR (Sạc không đầy).
 */
void Route::evaluate() {
    // 1. Reset trạng thái
    states.clear();
    states.resize(nodeSequence.size());
    evalResult = EvaluationResult(); // Mặc định feasible = true

    // Giá trị epsilon để so sánh số thực
    const double EPSILON = 1e-9;

    // 2. Tìm trạm sạc CUỐI CÙNG trong tuyến (cho logic PR)
    int last_station_idx = -1;
    bool has_recharged = false;
    for (int i = nodeSequence.size() - 2; i >= 1; --i) {
        if (std::dynamic_pointer_cast<Station>(instance->getNodeById(nodeSequence[i]))) {
            last_station_idx = i;
            has_recharged = true;
            break;
        }
    }

    // 3. Khởi tạo trạng thái tại Depot (bắt đầu)
    auto start_depot = std::dynamic_pointer_cast<Depot>(instance->getNodeById(0));
    states[0].arrivalTime = start_depot->getReadyTime();
    states[0].departureTime = start_depot->getReadyTime();
    states[0].remainingBattery = vehicle->getBatteryCapacity(); // Pin đầy
    states[0].remainingLoad = vehicle->getCapacity();
    states[0].chargeAmount = 0.0;

    // 4. Lặp qua từng chặng (từ i đến i+1)
    for (size_t i = 0; i < nodeSequence.size() - 1; ++i) {
        int from_id = nodeSequence[i];
        int to_id = nodeSequence[i + 1];
        auto to_node = instance->getNodeById(to_id);

        RouteState& from_state = states[i];
        RouteState& to_state = states[i + 1];

        // 4.1. Tính toán di chuyển
        double distance = instance->getDistance(from_id, to_id);
        double travel_time = instance->getTime(from_id, to_id);
        double energy_consumed = distance * vehicle->getEnergyConsumptionRate();

        evalResult.totalDistance += distance;

        // 4.2. Cập nhật trạng thái khi "đến" node (trước khi phục vụ/sạc)
        to_state.arrivalTime = from_state.departureTime + travel_time;
        to_state.remainingBattery = from_state.remainingBattery - energy_consumed;
        to_state.remainingLoad = from_state.remainingLoad; // Tải trọng chưa đổi
        to_state.chargeAmount = 0.0;

        // 4.3. RÀNG BUỘC: HẾT PIN (CONSTRAINT VIOLATION 1)
        if (to_state.remainingBattery < -EPSILON) {
            evalResult.feasible = false;
            return; // Dừng ngay
        }

        // 4.4. Xử lý logic tại node "to_node"
        if (auto customer = std::dynamic_pointer_cast<Customer>(to_node)) {
            // ----- ĐÂY LÀ KHÁCH HÀNG -----

            // 4.4.1. RÀNG BUỘC: QUÁ TẢI (CONSTRAINT VIOLATION 2)
            to_state.remainingLoad -= customer->getDemand();
            if (to_state.remainingLoad < -EPSILON) {
                evalResult.feasible = false;
                return; // Dừng
            }

            // 4.4.2. RÀNG BUỘC: CỬA SỔ THỜI GIAN (CONSTRAINT VIOLATION 3)
            double wait_time = std::max(0.0, customer->getReadyTime() - to_state.arrivalTime);
            evalResult.totalWaitTime += wait_time;

            double service_start_time = to_state.arrivalTime + wait_time;

            if (service_start_time > customer->getDueDate() + EPSILON) {
                evalResult.feasible = false;
                return; // Dừng, đến quá muộn
            }

            // 4.4.3. Cập nhật thời gian rời đi
            to_state.departureTime = service_start_time + customer->getServiceTime();

        } else if (auto station = std::dynamic_pointer_cast<Station>(to_node)) {
            // ----- ĐÂY LÀ TRẠM SẠC -----

            double charge_needed = 0.0;
            if (i + 1 == last_station_idx) {
                // *** LOGIC SẠC KHÔNG ĐẦY (PARTIAL RECHARGE) ***
                // Đây là trạm cuối, chỉ sạc đủ để về depot
                double energy_to_depot = 0.0;
                for (size_t j = i + 1; j < nodeSequence.size() - 1; ++j) {
                    energy_to_depot += instance->getDistance(nodeSequence[j], nodeSequence[j+1])
                                     * vehicle->getEnergyConsumptionRate();
                }

                charge_needed = std::max(0.0, energy_to_depot - to_state.remainingBattery);

            } else {
                // *** LOGIC SẠC ĐẦY (FULL RECHARGE) ***
                // Đây là trạm trung gian, sạc đầy để đi tiếp.
                charge_needed = vehicle->getBatteryCapacity() - to_state.remainingBattery;
            }

            // 4.4.4. RÀNG BUỘC: SẠC KHÔNG THỂ (CONSTRAINT VIOLATION 4)
            // (Xảy ra khi sạc PR mà pin vẫn không đủ, vd: đến 10%, cần 100% để về)
            double max_charge_possible = vehicle->getBatteryCapacity() - to_state.remainingBattery;
            if (charge_needed > max_charge_possible + EPSILON) {
                evalResult.feasible = false;
                return; // Dừng, không thể sạc đủ
            }

            to_state.chargeAmount = charge_needed;

            // 4.4.5. Tính thời gian sạc và cập nhật trạng thái
            double charge_time = to_state.chargeAmount * station->getChargingRate();
            evalResult.totalChargeTime += charge_time;
            evalResult.totalChargeAmount += to_state.chargeAmount;

            to_state.departureTime = to_state.arrivalTime + charge_time;
            to_state.remainingBattery += to_state.chargeAmount;

        } else {
            // ----- ĐÂY LÀ DEPOT (ĐIỂM KẾT THÚC) -----
            auto end_depot = std::dynamic_pointer_cast<Depot>(to_node);

            // 4.4.6. RÀNG BUỘC: VỀ DEPOT QUÁ MUỘN (CONSTRAINT VIOLATION 5)
            if (to_state.arrivalTime > end_depot->getLastTime() + EPSILON) {
                evalResult.feasible = false;
                return; // Dừng [cite: 211]
            }

            // (Kiểm tra logic PR)
            // Nếu đã sạc, pin khi về depot phải gần bằng 0
            if (has_recharged && std::abs(to_state.remainingBattery) > EPSILON) {
                // Lỗi logic: Lượng sạc PR tính toán bị sai
                // Trong thực tế, phép tính ở trạm cuối cùng đã đảm bảo điều này.
                // Dòng này để đề phòng lỗi logic.
                // std::cout << "Cảnh báo: Logic PR tính sai, pin về depot không rỗng." << std::endl;
            }

            to_state.departureTime = to_state.arrivalTime; // Kết thúc tuyến
        }
    } // Kết thúc vòng lặp

    // 5. Nếu đến được đây, tuyến đường khả thi
    evalResult.feasible = true;
    evalResult.totalTime = states.back().arrivalTime - states.front().departureTime;
}


// -----------------------------------------------------------------
// CÁC HÀM GETTER (Chỉ trả về kết quả đã tính toán)
// -----------------------------------------------------------------

int Route::getId() const{
    return id;
}
const std::vector<int>& Route::getNodes() const{
    return nodeSequence;
}

bool Route::isFeasible() const{
    return evalResult.feasible;
}

double Route::getTotalDistance() const{
    return evalResult.totalDistance;
}

double Route::getTotalWaitTime() const{
    return evalResult.totalWaitTime;
}

double Route::getTotalChargeTime() const{
    return evalResult.totalChargeTime;
}

double Route::getTotalChargeAmount() const{
    return evalResult.totalChargeAmount;
}

double Route::getTotalTime() const{
    return evalResult.totalTime;
}

void Route::toString() const {
    std::cout << "Route Id:" << id <<std::endl;
    for (size_t i = 0; i < nodeSequence.size() - 1; ++i) {
        std::cout << nodeSequence[i] << "->";
    }
    std::cout << nodeSequence[nodeSequence.size() - 1] << std::endl;
}