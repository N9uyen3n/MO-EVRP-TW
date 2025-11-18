#include "../../include/core/Route.h"
#include "../../include/core/Instance.h"
#include "../../include/core/Customer.h"
#include "../../include/core/Station.h"
#include "../../include/core/Depot.h"
#include <stdexcept>
#include <algorithm> // Cho std::max
#include <cmath>     // Cho std::abs (kiểm tra số thực)
#include <iostream>
#include <ostream>
#include <sstream>   // <-- Cần include thư viện này
#include <iomanip>

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
    // evaluate(); // Đánh giá lại tuyến đường
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
    // evaluate(); // Đánh giá lại tuyến đường
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
 * @brief Đảo node
 */

void Route::reverseNodes(size_t i, size_t j) {
    // Kiểm tra an toàn:
    // i và j phải là chỉ số hợp lệ, không phải depot (index 0 và size-1).
    // i phải nhỏ hơn j.
    if (i < 1 || j >= nodeSequence.size() - 1 || i >= j) {
        // (Logic findBestTwoOptMove nên ngăn điều này xảy ra)
        throw std::out_of_range("Chi so 2-Opt không hợp lệ.");
    }

    // Đảo ngược đoạn [i, j] (iterator thứ 2 là "one-past-the-end")
    std::reverse(nodeSequence.begin() + i, nodeSequence.begin() + j + 1);

    // KHÔNG GỌI evaluate() ở đây.
    // evaluateMove và applyMove trong LocalSearch.cpp sẽ gọi nó.
}

/**
 * @brief Hàm "trái tim" của Route.
 * Tính toán tất cả chi phí và kiểm tra tính khả thi.
 * Thực thi logic EVRPTW-PR (Sạc không đầy).
 */


/**
 * @brief ĐÁNH GIÁ TUYẾN ĐƯỜNG (PHIÊN BẢN TỐI ƯU HÓA DP)
 *
 * Hàm này triển khai thuật toán 2 bước (2-pass) O(N) để
 * tính toán tính khả thi VÀ lượng sạc tối thiểu.
 *
 * 1. Bước 1 (Duyệt ngược): Dùng DP tính 'dp[i]', là lượng pin
 * TỐI THIỂU cần có khi RỜI node 'i' để hoàn thành tuyến.
 * 2. Bước 2 (Duyệt xuôi): Mô phỏng tuyến, kiểm tra TOÀN BỘ ràng buộc
 * (Pin, Tải, Time Window) và sạc theo 'dp[i]'.
 */
void Route::evaluate() {
    int n = nodeSequence.size();
    if (n <= 1) { // Tuyến rỗng (chỉ có 1 depot)
        evalResult = EvaluationResult(0.0, 0.0, 0.0, 0.0, 0.0, true);
        return;
    }

    std::vector<double> dp(n, 0.0);
    const double EPSILON = 1e-9;

    // ==========================================================
    // BƯỚC 1: DUYỆT NGƯỢC (BACKWARD PASS) - TÍNH TOÁN NĂNG LƯỢNG
    // (Logic DP của bạn)
    // ==========================================================
    dp[n - 1] = 0.0; // Depot cuối

    for (int i = n - 2; i >= 0; --i) {
        double energy_to_next = instance->getDistance(nodeSequence[i], nodeSequence[i+1])
                              * vehicle->getEnergyConsumptionRate();

        auto next_node = instance->getNodeById(nodeSequence[i+1]);

        if (std::dynamic_pointer_cast<Station>(next_node)) {
            // Node tiếp theo là trạm → có thể sạc đầy
            dp[i] = energy_to_next + std::max(0.0, dp[i+1] - vehicle->getBatteryCapacity());
        } else {
            // Không sạc → phải mang đủ pin
            dp[i] = energy_to_next + dp[i+1];
        }

        // KIỂM TRA KHẢ THI NĂNG LƯỢNG (Rất quan trọng)
        if (dp[i] > vehicle->getBatteryCapacity() + EPSILON) {
            evalResult = EvaluationResult(); // Mặc định là infeasible
            evalResult.feasible = false;
            return; // INFEASIBLE (Trường hợp 2 & 3: khoảng cách quá dài)
        }
    }

    // ==========================================================
    // BƯỚC 2: DUYỆT XUÔI (FORWARD PASS) - MÔ PHỎNG & KIỂM TRA
    // (Kết hợp logic DP và các ràng buộc cũ của bạn)
    // ==========================================================
    states.clear();
    states.resize(n);
    evalResult = EvaluationResult(); // Reset kết quả (feasible = true)

    // Khởi tạo Depot đầu
    auto start_depot = std::dynamic_pointer_cast<Depot>(instance->getNodeById(0));
    states[0].arrivalTime = start_depot->getReadyTime();
    states[0].departureTime = start_depot->getReadyTime();
    states[0].remainingBattery = vehicle->getBatteryCapacity(); // Pin đầy
    states[0].remainingLoad = vehicle->getCapacity();
    states[0].chargeAmount = 0.0;

    // Lặp qua từng chặng
    for (int i = 0; i < n - 1; ++i) {
        int from_id = nodeSequence[i];
        int to_id = nodeSequence[i + 1];
        auto to_node = instance->getNodeById(to_id);

        RouteState& from_state = states[i];
        RouteState& to_state = states[i + 1];

        // 2.1. Tính toán di chuyển
        double distance = instance->getDistance(from_id, to_id);
        double travel_time = instance->getTime(from_id, to_id);
        double energy_consumed = distance * vehicle->getEnergyConsumptionRate();

        evalResult.totalDistance += distance;

        // 2.2. Cập nhật trạng thái "đến"
        to_state.arrivalTime = from_state.departureTime + travel_time;
        to_state.remainingBattery = from_state.remainingBattery - energy_consumed;
        to_state.remainingLoad = from_state.remainingLoad;
        to_state.chargeAmount = 0.0;

        // 2.3. RÀNG BUỘC 1: HẾT PIN TRÊN ĐƯỜNG
        // (dp[0] đã check pin tối thiểu, nhưng nếu pin xuất phát không đầy
        //  thì vẫn có thể lỗi ở đây)
        if (to_state.remainingBattery < -EPSILON) {
            evalResult.feasible = false;
            return;
        }

        // 2.4. Xử lý logic tại node "to_node"
        if (auto customer = std::dynamic_pointer_cast<Customer>(to_node)) {
            // ----- ĐÂY LÀ KHÁCH HÀNG -----

            // RÀNG BUỘC 2: QUÁ TẢI
            to_state.remainingLoad -= customer->getDemand();
            if (to_state.remainingLoad < -EPSILON) {
                evalResult.feasible = false;
                return;
            }

            // RÀNG BUỘC 3: TIME WINDOW
            double wait_time = std::max(0.0, customer->getReadyTime() - to_state.arrivalTime);
            evalResult.totalWaitTime += wait_time;

            double service_start_time = to_state.arrivalTime + wait_time;

            if (service_start_time > customer->getDueDate() + EPSILON) {
                // std::cerr << "DEBUG ROUTE: FAILED (Cust " << to_node->getId() << " TW DueDate)" << std::endl;
                evalResult.feasible = false; // Đến quá muộn
                return;
            }

            states[i+1].departureTime = service_start_time + customer->getServiceTime();

        } else if (auto station = std::dynamic_pointer_cast<Station>(to_node)) {
            // ----- ĐÂY LÀ TRẠM SẠC -----

            // RÀNG BUỘC 3: TIME WINDOW (Kiểm tra khi đến)
            double wait_time = std::max(0.0, station->getReadyTime() - to_state.arrivalTime);
            evalResult.totalWaitTime += wait_time;

            double charge_start_time = to_state.arrivalTime + wait_time;

            if (charge_start_time > station->getDueDate() + EPSILON) {
                // std::cerr << "DEBUG ROUTE: FAILED (Station " << to_node->getId() << " TW DueDate)" << std::endl;
                evalResult.feasible = false; // Đến trạm quá muộn
                return;
            }

            // LOGIC SẠC TỐI ƯU (từ DP)
            // dp[i+1] là lượng pin TỐI THIỂU cần có khi RỜI trạm này
            double charge_needed = std::max(0.0, dp[i+1] - to_state.remainingBattery);

            // Đảm bảo không sạc quá dung lượng xe
            double charge_possible = vehicle->getBatteryCapacity() - to_state.remainingBattery;
            double charge_amount = std::min(charge_needed, charge_possible);

            double charge_time = charge_amount * station->getChargingRate();

            states[i+1].chargeAmount = charge_amount;
            evalResult.totalChargeTime += charge_time;
            evalResult.totalChargeAmount += charge_amount;

            states[i+1].departureTime = charge_start_time + charge_time;
            states[i+1].remainingBattery += charge_amount;

            // RÀNG BUỘC 3: TIME WINDOW (Kiểm tra khi rời đi)
            if (states[i+1].departureTime > station->getDueDate() + EPSILON) {
                evalResult.feasible = false; // Sạc quá lâu, vi phạm TW
                return;
            }

        } else if (auto depot = std::dynamic_pointer_cast<Depot>(to_node)) {
            // ----- ĐÂY LÀ DEPOT (KẾT THÚC) -----

            // RÀNG BUỘC 3: TIME WINDOW
            if (to_state.arrivalTime > depot->getDueDate() + EPSILON) {
                // std::cerr << "DEBUG ROUTE: FAILED (Depot " << to_node->getId() << " TW DueDate)" << std::endl;
                evalResult.feasible = false; // Về depot quá muộn
                return;
            }
            states[i+1].departureTime = to_state.arrivalTime;
            depot->setLastTime(to_state.arrivalTime);
        }
    }

    // Nếu đến được đây, tuyến đường khả thi
    evalResult.feasible = true;
    evalResult.totalTime = states.back().arrivalTime - states.front().departureTime;
    // (return true; không cần thiết vì đây là hàm void)
}


/**
 * @brief (HÀM NHANH O(N)) Triển khai mô phỏng delta-cost
 */
InsertionResult Route::checkInsertionCost(int customerId, size_t position) const {

    // --- 0. TẠO TUYẾN MÔ PHỎNG (O(N)) ---
    std::vector<int> simNodeSequence = nodeSequence;
    simNodeSequence.insert(simNodeSequence.begin() + position, customerId);

    int n = simNodeSequence.size();
    const double EPSILON = 1e-9;
    std::vector<double> dp(n, 0.0);

    // --- 1. TÍNH DELTA MỤC TIÊU (O(1)) ---
    // ==========================================================
    // BƯỚC 1 (Mô phỏng): DUYỆT NGƯỢC (BACKWARD PASS) - O(N)
    // (Y hệt logic DP của evaluate())
    // ==========================================================
    dp[n - 1] = 0.0; // Depot cuối

    for (int i = n - 2; i >= 0; --i) {
        double energy_to_next = instance->getDistance(simNodeSequence[i], simNodeSequence[i+1])
                              * vehicle->getEnergyConsumptionRate();

        auto next_node = instance->getNodeById(simNodeSequence[i+1]);

        if (std::dynamic_pointer_cast<Station>(next_node)) {
            dp[i] = energy_to_next + std::max(0.0, dp[i+1] - vehicle->getBatteryCapacity());
        } else {
            dp[i] = energy_to_next + dp[i+1];
        }

        if (dp[i] > vehicle->getBatteryCapacity() + EPSILON) {
            return { false }; // INFEASIBLE (Khoảng cách quá dài)
        }
    }

    // ==========================================================
    // BƯỚC 2 (Mô phỏng): DUYỆT XUÔI (FORWARD PASS) - O(N)
    // (Y hệt logic DP của evaluate())
    // ==========================================================
    std::vector<RouteState> simStates(n);
    double simTotalWaitTime = 0.0;
    double simTotalChargeAmount = 0.0;

    // *** THAY ĐỔI 1: Khởi tạo simTotalDistance ***
    double simTotalDistance = 0.0;

    auto start_depot = std::dynamic_pointer_cast<Depot>(instance->getNodeById(0));
    simStates[0].arrivalTime = start_depot->getReadyTime();
    simStates[0].departureTime = start_depot->getReadyTime();
    simStates[0].remainingBattery = vehicle->getBatteryCapacity();
    simStates[0].remainingLoad = vehicle->getCapacity();

    for (int i = 0; i < n - 1; ++i) {
        int from_id = simNodeSequence[i];
        int to_id = simNodeSequence[i + 1];
        auto to_node = instance->getNodeById(to_id);

        RouteState& from_state = simStates[i];
        RouteState& to_state = simStates[i + 1];

        double distance = instance->getDistance(from_id, to_id);
        double travel_time = instance->getTime(from_id, to_id);
        double energy_consumed = distance * vehicle->getEnergyConsumptionRate();

        // *** THAY ĐỔI 2: Tích lũy simTotalDistance ***
        simTotalDistance += distance;

        to_state.arrivalTime = from_state.departureTime + travel_time;
        to_state.remainingBattery = from_state.remainingBattery - energy_consumed;
        to_state.remainingLoad = from_state.remainingLoad;
        to_state.chargeAmount = 0.0;

        if (to_state.remainingBattery < -EPSILON) return { false };

        if (auto cust = std::dynamic_pointer_cast<Customer>(to_node)) {
            to_state.remainingLoad -= cust->getDemand();
            if (to_state.remainingLoad < -EPSILON) return { false }; // Lỗi tải

            double wait_time = std::max(0.0, cust->getReadyTime() - to_state.arrivalTime);
            simTotalWaitTime += wait_time;

            double service_start_time = to_state.arrivalTime + wait_time;
            if (service_start_time > cust->getDueDate() + EPSILON) return { false };
            to_state.departureTime = service_start_time + cust->getServiceTime();

        } else if (auto station = std::dynamic_pointer_cast<Station>(to_node)) {
            double wait_time = std::max(0.0, station->getReadyTime() - to_state.arrivalTime);
            simTotalWaitTime += wait_time;
            double charge_start_time = to_state.arrivalTime + wait_time;
            if (charge_start_time > station->getDueDate() + EPSILON) return { false };

            double charge_needed = std::max(0.0, dp[i+1] - to_state.remainingBattery);
            double charge_possible = vehicle->getBatteryCapacity() - to_state.remainingBattery;
            double charge_amount = std::min(charge_needed, charge_possible);
            double charge_time = charge_amount * station->getChargingRate();

            to_state.chargeAmount = charge_amount;
            simTotalChargeAmount += charge_amount;

            to_state.departureTime = charge_start_time + charge_time;
            to_state.remainingBattery += charge_amount;

            if (to_state.departureTime > station->getDueDate() + EPSILON) return { false };

        } else if (auto depot = std::dynamic_pointer_cast<Depot>(to_node)) {
            if (to_state.arrivalTime > depot->getDueDate() + EPSILON) return { false };
            to_state.departureTime = to_state.arrivalTime;
            depot->setLastTime(to_state.arrivalTime);
        }
    }

    // 6. NẾU MỌI THỨ OK
    InsertionResult res;
    res.isFeasible = true;

    // *** THAY ĐỔI 3: Tính delta dựa trên tổng chi phí MỚI và CŨ ***
    res.deltaDistance = simTotalDistance - evalResult.totalDistance;

    // Tính delta so với giá trị đã cache của tuyến ĐANG CÓ
    res.deltaChargeAmount = simTotalChargeAmount - evalResult.totalChargeAmount;
    res.deltaWaitTime = simTotalWaitTime - evalResult.totalWaitTime;

    return res;
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

void Route::print() const {
    std::cout << "Route Id:" << id << " - Total Distance: " << evalResult.totalDistance <<
        " - Total Time: " << evalResult.totalTime <<
        " - Total Charge Amount: " << evalResult.totalChargeAmount << std::endl;
    for (size_t i = 0; i < nodeSequence.size() - 1; ++i) {
        std::cout << nodeSequence[i] << "->";
    }
    std::cout << nodeSequence[nodeSequence.size() - 1] << std::endl;
}

// std::string Route::toString() const {
//     // 1. Tạo một stringstream để "xây dựng" chuỗi
//     std::stringstream ss;
//     ss << std::fixed << std::setprecision(2); // Đặt định dạng số float (ví dụ: 123.45)
//
//     // 2. Chuyển đổi dòng std::cout đầu tiên
//     //    (Thay std::cout bằng ss, và std::endl bằng "\n")
//     ss << "Route Id:" << id << " - Total Distance: " << evalResult.totalDistance <<
//         " - Total Time: " << evalResult.totalTime <<
//         " - Total Charge Amount: " << evalResult.totalChargeAmount << "\n";
//
//     // 3. Chuyển đổi vòng lặp (Thay std::cout bằng ss)
//     for (size_t i = 0; i < nodeSequence.size() - 1; ++i) {
//         ss << nodeSequence[i] << ":" << this->states[i].arrivalTime << this->states[i].departureTime << "->";
//     }
//
//
//     // 4. Chuyển đổi dòng std::cout cuối cùng
//     //    (Thêm kiểm tra an toàn nếu tuyến rỗng)
//     if (!nodeSequence.empty()) {
//         ss << nodeSequence[nodeSequence.size() - 1] << "\n";
//     }
//
//     // 5. Trả về chuỗi kết quả
//     return ss.str();
// }

std::string Route::toString() const {
    // 1. Tạo một stringstream và cài đặt định dạng số
    std::stringstream ss;
    ss << std::fixed << std::setprecision(2);

    // 2. In thông tin tóm tắt của toàn bộ tuyến đường
    ss << "--- Route ID: " << this->id << " ---"
       << " Feasible: " << (this->evalResult.feasible ? "YES" : "NO") << "\n"
       << "   Total Distance:    " << std::setw(8) << this->evalResult.totalDistance << "\n"
       << "   Total Time:        " << std::setw(8) << this->evalResult.totalTime << "\n"
       // Các dòng tóm tắt khác (wait time, charge time, charge amount)
       // bạn có thể thêm vào tương tự nếu cần.
       << "   Total Charge Amt:  " << std::setw(8) << this->evalResult.totalChargeAmount << "\n";


    // 3. Kiểm tra các trường hợp biên
    if (nodeSequence.empty()) {
        ss << "   [Route is Empty]\n";
        return ss.str();
    }
    if (nodeSequence.size() != states.size()) {
        ss << "   [ERROR: Node sequence and states vectors have different sizes!]\n"
           << "   NodeSequence.size() = " << nodeSequence.size() << "\n"
           << "   States.size() = " << states.size() << "\n";
        return ss.str();
    }

    // 4. In tiêu đề cho bảng thông tin chi tiết các node
    ss << "--- Node Sequence (Count: " << nodeSequence.size() << ") ---\n";
    ss << std::setw(5) << "Idx" << " | "
       << std::setw(5) << "StrID" << " | "
       << std::setw(10) << "Type" << " | "
       << std::setw(8) << "ArrTime" << " | "
       << std::setw(8) << "DepTime" << " | "
       << std::setw(8) << "RemBat" << " | "
       << std::setw(8) << "RemLoad" << " | "
       << std::setw(8) << "Charge" << "\n";
    ss << "-----------------------------------------------------------------------------------\n";

    // 5. Lặp qua từng node và trạng thái tương ứng
    for (size_t i = 0; i < nodeSequence.size(); ++i) {
        int nodeId = nodeSequence[i];     // Lấy ID từ sequence
        const auto& state = states[i];  // Lấy trạng thái động (dynamic state)

        // --- GỌI HÀM CỦA INSTANCE ---
        // Đây là phần quan trọng:
        // Chúng ta dùng 'instance' để lấy đối tượng Node (shared_ptr) từ 'nodeId'.
        const std::shared_ptr<Node>& node = this->instance->getNodeById(nodeId);

        // Chuyển loại node (enum) thành chuỗi để dễ đọc
        std::string nodeTypeStr = "???";
        if (std::dynamic_pointer_cast<Customer>(node)) { // Kiểm tra xem node có null không
            nodeTypeStr = "Customer";
        } else if (std::dynamic_pointer_cast<Station>(node)) {
            nodeTypeStr = "Station";
        } else if (std::dynamic_pointer_cast<Depot>(node)) {
            nodeTypeStr = "Depot";
        }

        // In ra dòng dữ liệu đã được căn chỉnh
        ss << std::setw(5) << node->getId() << " | "
           << std::setw(5) << node->getStringId() << " | "
           << std::setw(10) << nodeTypeStr << " | "
           << std::setw(8) << state.arrivalTime << " | "
           << std::setw(8) << state.departureTime << " | "
           << std::setw(8) << state.remainingBattery << " | "
           << std::setw(8) << state.remainingLoad << " | "
           << std::setw(8) << state.chargeAmount << "\n";
    }
    ss << "\n";

    // 6. Trả về chuỗi kết quả
    return ss.str();
}