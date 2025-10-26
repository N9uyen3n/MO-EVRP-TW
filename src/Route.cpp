#include "../include/Route.h"
#include "../include/Utils.h"
#include "../include/Customer.h"
#include "../include/Station.h"
#include "../include/Depot.h"
#include <numeric>      // Dùng cho std::accumulate
#include <algorithm>    // Dùng cho std::max

/**
 * @brief Hàm khởi tạo cho lớp Route.
 * 
 * @param id Mã định danh của tuyến đường.
 * @param vehicle Con trỏ chia sẻ đến đối tượng Vehicle sẽ thực hiện tuyến đường này.
 * @param instance Con trỏ chia sẻ đến đối tượng Instance chứa thông tin của bài toán (ma trận khoảng cách, thời gian,...).
 */
Route::Route(int id, std::shared_ptr<Vehicle> vehicle, std::shared_ptr<const Instance> instance)
    : id(id), vehicle(std::move(vehicle)), instance(std::move(instance)), cacheValid(false) {}

/**
 * @brief Lấy mã định danh (ID) của tuyến đường.
 * @return ID của tuyến đường.
 */
int Route::getId() const {
    return id;
}

/**
 * @brief Lấy con trỏ đến đối tượng xe (Vehicle) được gán cho tuyến đường này.
 * @return Con trỏ chia sẻ đến đối tượng Vehicle.
 */
std::shared_ptr<Vehicle> Route::getVehicle() const {
    return vehicle;
}

/**
 * @brief Lấy danh sách các thông tin (RouteInfo) của các điểm trên tuyến đường.
 * Mỗi RouteInfo chứa thông tin về một điểm (node) và trạng thái của xe tại điểm đó.
 * @return Vector chứa các đối tượng RouteInfo.
 */
const std::vector<RouteInfo>& Route::getInfos() const {
    return infos;
}

/**
 * @brief Đếm số lượng khách hàng trên tuyến đường.
 * @return Số lượng khách hàng.
 */
int Route::getCustomerCount() const {
    int count = 0;
    for (const auto& info : infos) {
        // Kiểm tra xem node có phải là một Customer hay không
        if (dynamic_cast<const Customer*>(info.node.get())) {
            count++;
        }
    }
    return count;
}

// --- Triển khai Caching (Bộ nhớ đệm) ---

/**
 * @brief Vô hiệu hóa bộ nhớ đệm (cache).
 * Hàm này được gọi mỗi khi tuyến đường bị thay đổi (chèn/xóa điểm),
 * để đảm bảo các giá trị tính toán lại trong lần truy cập tiếp theo.
 */
void Route::invalidateCache() {
    cacheValid = false;
}

/**
 * @brief Xây dựng lại bộ nhớ đệm nếu nó không hợp lệ.
 * Hàm này tính toán lại các giá trị tổng hợp như tổng khoảng cách, tổng thời gian,
 * và tổng năng lượng đã sạc, sau đó lưu chúng vào cache để truy cập nhanh hơn.
 */
void Route::rebuildCache() const {
    if (cacheValid) return; // Nếu cache đã hợp lệ thì không cần làm gì

    // Tính tổng khoảng cách
    if (infos.size() < 2) {
        cachedTotalDistance = 0.0;
    } else {
        if (instance) {
            double totalDistance = 0.0;
            for (size_t i = 0; i < infos.size() - 1; ++i) {
                totalDistance += instance->getDistance(infos[i].node->getId(), infos[i+1].node->getId());
            }
            cachedTotalDistance = totalDistance;
        }
    }

    // Tính tổng thời gian (dựa vào thời gian đến điểm cuối cùng)
    if (infos.empty()) {
        cachedTotalTime = 0.0;
    } else {
        cachedTotalTime = infos.back().arrival_time;
    }

    // Tính tổng năng lượng đã sạc
    double totalEnergy = 0.0;
    for (const auto& info : infos) {
        // Nếu điểm là một trạm sạc
        if (dynamic_cast<const Station*>(info.node.get())) {
            totalEnergy += (info.departure_battery - info.arrival_battery); // Năng lượng lúc rời đi - năng lượng lúc đến
        }
    }
    cachedTotalEnergyCharged = totalEnergy;

    cacheValid = true; // Đánh dấu cache là hợp lệ
}

// --- Các hàm Getters sử dụng cache ---

/**
 * @brief Lấy tổng khoảng cách của tuyến đường.
 * Sử dụng cache để tăng hiệu suất.
 * @return Tổng khoảng cách.
 */
double Route::getTotalDistance() const {
    rebuildCache(); // Đảm bảo cache là mới nhất
    return cachedTotalDistance;
}

/**
 * @brief Lấy tổng thời gian của tuyến đường.
 * Sử dụng cache để tăng hiệu suất.
 * @return Tổng thời gian.
 */
double Route::getTotalTime() const {
    rebuildCache(); // Đảm bảo cache là mới nhất
    return cachedTotalTime;
}

/**
 * @brief Lấy tổng năng lượng đã được sạc trên tuyến đường.
 * Sử dụng cache để tăng hiệu suất.
 * @return Tổng năng lượng đã sạc.
 */
double Route::getTotalEnergyCharged() const {
    rebuildCache(); // Đảm bảo cache là mới nhất
    return cachedTotalEnergyCharged;
}

// --- Các phương thức sửa đổi tuyến đường ---

/**
 * @brief Kiểm tra xem có thể chèn một điểm (node) vào vị trí (position) đã cho hay không.
 * Hàm này thực hiện các kiểm tra ràng buộc quan trọng.
 * @param node Điểm cần chèn.
 * @param position Vị trí muốn chèn (index trong vector `infos`).
 * @return true nếu có thể chèn, false nếu không.
 */
bool Route::canInsert(std::shared_ptr<Node> node, size_t position) {
    // Vị trí chèn phải hợp lệ (không phải đầu hoặc cuối)
    if (!instance || position <= 0 || position >= infos.size()) {
        return false;
    }

    auto customer_to_insert = std::dynamic_pointer_cast<Customer>(node);
    if (!customer_to_insert) {
        // Hiện tại chỉ xử lý chèn khách hàng.
        return false;
    }

    const RouteInfo& prev_info = infos[position - 1]; // Điểm ngay trước vị trí chèn
    const RouteInfo& next_info_original = infos[position]; // Điểm ngay sau vị trí chèn

    // --- Ràng buộc 1: Tải trọng (Capacity) ---
    // Tổng tải trọng mới không được vượt quá sức chứa của xe.
    double current_total_demand = 0;
    for(const auto& info : infos) {
        if(auto c = std::dynamic_pointer_cast<Customer>(info.node)) {
            current_total_demand += c->getDemand();
        }
    }
    if (current_total_demand + customer_to_insert->getDemand() > vehicle->getCapacity()) {
        return false;
    }

    // --- Ràng buộc 2 & 3: Cửa sổ thời gian (Time Windows) và Năng lượng (Battery) ---
    // Mô phỏng trạng thái tại điểm mới và điểm kế tiếp nó.

    // Trạng thái tại điểm chèn
    double travel_time_to_new = instance->getTime(prev_info.node->getId(), node->getId());
    double energy_to_new = instance->getDistance(prev_info.node->getId(), node->getId()) * instance->getVehicleEnergyRate();

    double arrival_time_at_new = prev_info.departure_time + travel_time_to_new;
    double arrival_battery_at_new = prev_info.departure_battery - energy_to_new;

    if (arrival_battery_at_new < 0) return false; // Không đủ năng lượng để đến điểm mới

    double wait_time_at_new = std::max(0.0, customer_to_insert->getReadyTime() - arrival_time_at_new);
    if (arrival_time_at_new + wait_time_at_new > customer_to_insert->getDueDate()) return false; // Vi phạm cửa sổ thời gian của điểm mới

    double departure_time_from_new = arrival_time_at_new + wait_time_at_new + customer_to_insert->getServiceTime();

    // Trạng thái tại điểm tiếp theo trong tuyến đường ban đầu
    double travel_time_to_next = instance->getTime(node->getId(), next_info_original.node->getId());
    double energy_to_next = instance->getDistance(node->getId(), next_info_original.node->getId()) * instance->getVehicleEnergyRate();

    double arrival_time_at_next = departure_time_from_new + travel_time_to_next;
    double arrival_battery_at_next = arrival_battery_at_new - energy_to_next; // Pin không đổi tại điểm khách hàng

    if (arrival_battery_at_next < 0) return false; // Không đủ năng lượng để đến điểm tiếp theo

    if (auto next_customer = std::dynamic_pointer_cast<const Customer>(next_info_original.node)) {
        if (arrival_time_at_next > next_customer->getDueDate()) return false; // Việc chèn làm cho điểm tiếp theo bị trễ
    }
    
    // Một kiểm tra đầy đủ sẽ yêu cầu lan truyền các thay đổi về thời gian cho toàn bộ phần còn lại của tuyến đường.
    // Kiểm tra đơn giản này là một heuristic tốt.
    return true;
}

/**
 * @brief Chèn một điểm vào tuyến đường tại một vị trí cụ thể.
 * @param node Điểm cần chèn.
 * @param position Vị trí để chèn.
 */
void Route::insert(std::shared_ptr<Node> node, size_t position) {
    if (position > infos.size()) {
        position = infos.size();
    }
    RouteInfo newInfo;
    newInfo.node = node;

    infos.insert(infos.begin() + position, newInfo);
    recalculateFrom(0); // Tính toán lại toàn bộ tuyến đường từ depot
}

/**
 * @brief Xóa một điểm khỏi tuyến đường tại một vị trí cụ thể.
 * @param position Vị trí của điểm cần xóa.
 */
void Route::remove(size_t position) {
    if (position > 0 && position < infos.size()) { // Không thể xóa depot
        infos.erase(infos.begin() + position);
        recalculateFrom(0); // Tính toán lại toàn bộ tuyến đường từ depot
    }
}

/**
 * @brief Tính toán lại tất cả các thông số của tuyến đường (thời gian, tải trọng, pin) bắt đầu từ một vị trí.
 * Đây là hàm logic cốt lõi, cập nhật trạng thái của xe tại mỗi điểm.
 * @param position Vị trí bắt đầu tính toán lại (thường là 0 để tính lại từ đầu).
 */
void Route::recalculateFrom(size_t position) {
    if (infos.empty() || !instance) return;

    // --- Logic giao hàng ---
    // 1. Tính tổng nhu cầu (demand) cho tuyến đường này.
    double total_demand = 0;
    for(const auto& info : infos) {
        if(auto c = std::dynamic_pointer_cast<Customer>(info.node)) {
            total_demand += c->getDemand();
        }
    }

    // 2. Khởi tạo trạng thái tại depot xuất phát (luôn ở index 0)
    auto& depot_info = infos[0];
    depot_info.arrival_time = 0;
    depot_info.departure_time = 0;
    depot_info.arrival_load = total_demand; // Đến depot với tải trọng bằng 0, nhưng khi rời đi sẽ lấy hàng
    depot_info.departure_load = total_demand; // Rời đi với đủ hàng cho cả tuyến
    depot_info.arrival_battery = instance->getVehicleBattery(); // Pin đầy tại depot
    depot_info.departure_battery = instance->getVehicleBattery();

    // 3. Tính toán lại tất cả các điểm tiếp theo
    for (size_t i = 1; i < infos.size(); ++i) {
        RouteInfo& prev_info = infos[i-1];
        RouteInfo& current_info = infos[i];
        
        double travel_time = instance->getTime(prev_info.node->getId(), current_info.node->getId());
        double travel_dist = instance->getDistance(prev_info.node->getId(), current_info.node->getId());
        double energy_consumed = travel_dist * instance->getVehicleEnergyRate();

        current_info.arrival_time = prev_info.departure_time + travel_time;
        current_info.arrival_battery = prev_info.departure_battery - energy_consumed;
        current_info.arrival_load = prev_info.departure_load;

        if (auto customer = std::dynamic_pointer_cast<Customer>(current_info.node)) {
            // Logic cho khách hàng
            double wait_time = std::max(0.0, customer->getReadyTime() - current_info.arrival_time);
            current_info.departure_time = current_info.arrival_time + wait_time + customer->getServiceTime();
            current_info.departure_load = current_info.arrival_load - customer->getDemand(); // Giao hàng: tải trọng giảm
            current_info.departure_battery = current_info.arrival_battery; // Pin không đổi
        } else if (auto station = std::dynamic_pointer_cast<Station>(current_info.node)) {
            // Logic cho trạm sạc
            // Chiến lược đơn giản: sạc đầy. Có thể cải tiến.
            double battery_to_charge = instance->getVehicleBattery() - current_info.arrival_battery;
            double charging_time = (station->getChargingRate() > 0) ? (battery_to_charge / station->getChargingRate()) : 0;
            
            current_info.departure_time = current_info.arrival_time + charging_time;
            current_info.departure_battery = instance->getVehicleBattery(); // Pin đầy sau khi sạc
            current_info.departure_load = current_info.arrival_load; // Tải trọng không đổi
        } else if (auto depot = std::dynamic_pointer_cast<Depot>(current_info.node)) {
            // Logic cho depot kết thúc
            current_info.departure_time = current_info.arrival_time;
            current_info.departure_battery = current_info.arrival_battery;
            current_info.departure_load = current_info.arrival_load;
        }
    }

    invalidateCache(); // Vô hiệu hóa cache vì tuyến đường đã thay đổi
}

// --- Các phương thức cho toán tử ALNS ---

/**
 * @brief Tính toán chi phí (thay đổi về khoảng cách) khi chèn một điểm vào vị trí đã cho.
 * Được sử dụng trong thuật toán ALNS để đánh giá một hành động chèn.
 * @param node Điểm cần chèn.
 * @param position Vị trí dự định chèn.
 * @return Chi phí chèn (chênh lệch khoảng cách).
 */
double Route::getInsertionCost(std::shared_ptr<Node> node, int position) const {
    if (!instance || infos.size() < 2 || position <= 0 || position >= infos.size()) {
        return std::numeric_limits<double>::max(); // Trả về chi phí vô cùng lớn nếu không hợp lệ
    }

    auto prev_node = infos[position - 1].node;
    auto next_node = infos[position].node;

    // Khoảng cách cũ: prev -> next
    double old_dist = instance->getDistance(prev_node->getId(), next_node->getId());
    // Khoảng cách mới: prev -> node -> next
    double new_dist = instance->getDistance(prev_node->getId(), node->getId()) + instance->getDistance(node->getId(), next_node->getId());

    return new_dist - old_dist; // Chi phí là sự chênh lệch
}

/**
 * @brief Tìm và xóa một khách hàng cụ thể khỏi tuyến đường.
 * @param customer Khách hàng cần xóa.
 * @return true nếu tìm thấy và xóa thành công, false nếu không.
 */
bool Route::removeCustomer(std::shared_ptr<Customer> customer) {
    for (size_t i = 0; i < infos.size(); ++i) {
        if (infos[i].node == customer) {
            remove(i); // Gọi hàm xóa theo vị trí
            return true;
        }
    }
    return false;
}