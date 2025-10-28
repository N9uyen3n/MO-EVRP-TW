#include "../include/Route.h"
#include "../include/Utils.h"
#include "../include/Customer.h"
#include "../include/Station.h"
#include "../include/Depot.h"
#include <numeric>
#include <algorithm>

// --- Hàm khởi tạo và các hàm getter đơn giản (không thay đổi nhiều) ---
Route::Route(int id, std::shared_ptr<Vehicle> vehicle, std::shared_ptr<const Instance> instance)
    : id(id), vehicle(std::move(vehicle)), instance(std::move(instance)), cacheValid(false) {}

int Route::getId() const { return id; }
std::shared_ptr<Vehicle> Route::getVehicle() const { return vehicle; }
const std::vector<RouteInfo>& Route::getInfos() const { return infos; }

int Route::getCustomerCount() const {
    int count = 0;
    for (const auto& info : infos) {
        if (dynamic_cast<const Customer*>(info.node.get())) {
            count++;
        }
    }
    return count;
}

// --- Quản lý bộ nhớ đệm (Caching) ---
void Route::invalidateCache() {
    cacheValid = false;
}

void Route::rebuildCache() const {
    if (cacheValid) return;

    if (infos.size() < 2) {
        cachedTotalDistance = 0.0;
    } else {
        double totalDistance = 0.0;
        for (size_t i = 0; i < infos.size() - 1; ++i) {
            totalDistance += instance->getDistance(infos[i].node->getId(), infos[i+1].node->getId());
        }
        cachedTotalDistance = totalDistance;
    }

    cachedTotalTime = infos.empty() ? 0.0 : infos.back().arrival_time;

    double totalEnergy = 0.0;
    for (const auto& info : infos) {
        if (dynamic_cast<const Station*>(info.node.get())) {
            totalEnergy += (info.departure_battery - info.arrival_battery);
        }
    }
    cachedTotalEnergyCharged = totalEnergy;

    cacheValid = true;
}

// --- Các hàm Getters sử dụng cache ---
double Route::getTotalDistance() const {
    rebuildCache();
    return cachedTotalDistance;
}

double Route::getTotalTime() const {
    rebuildCache();
    return cachedTotalTime;
}

double Route::getTotalEnergyCharged() const {
    rebuildCache();
    return cachedTotalEnergyCharged;
}

// --- LOGIC CỐT LÕI MỚI ---
bool Route::propogateAndUpdate(std::vector<RouteInfo>& route_infos, size_t start_index) {
    if (route_infos.empty() || !instance) return true; // Tuyến đường rỗng luôn hợp lệ

    // Nếu start_index = 0, tức là tính toán lại từ đầu (ví dụ: khi mới tạo tuyến).
    // Cần khởi tạo trạng thái cho depot.
    if (start_index == 0) {
        // Tính tổng nhu cầu hàng hóa cho cả tuyến để biết xe cần chở bao nhiêu từ depot.
        double total_demand = 0;
        for(const auto& info : route_infos) {
            if(auto c = std::dynamic_pointer_cast<Customer>(info.node)) {
                total_demand += c->getDemand();
            }
        }
        auto& depot_info = route_infos[0];
        depot_info.arrival_time = 0;
        depot_info.departure_time = 0;
        depot_info.arrival_load = total_demand; // Tải trọng khi đến depot (kết thúc) sẽ bằng 0, nhưng khi bắt đầu thì khác
        depot_info.departure_load = total_demand; // Rời depot với đầy đủ hàng
        depot_info.arrival_battery = instance->getVehicleBattery(); // Pin đầy
        depot_info.departure_battery = instance->getVehicleBattery();
        start_index = 1; // Bắt đầu lan truyền từ điểm tiếp theo (sau depot)
    }

    // Lan truyền các thay đổi từ vị trí `start_index`
    for (size_t i = start_index; i < route_infos.size(); ++i) {
        RouteInfo& prev_info = route_infos[i-1];
        RouteInfo& current_info = route_infos[i];
        
        // Tính toán thời gian và năng lượng tiêu thụ để đi từ điểm trước đến điểm hiện tại
        double travel_time = instance->getTime(prev_info.node->getId(), current_info.node->getId());
        double travel_dist = instance->getDistance(prev_info.node->getId(), current_info.node->getId());
        double energy_consumed = travel_dist * instance->getVehicleEnergyRate();

        // Cập nhật trạng thái khi "đến" điểm hiện tại
        current_info.arrival_time = prev_info.departure_time + travel_time;
        current_info.arrival_battery = prev_info.departure_battery - energy_consumed;
        current_info.arrival_load = prev_info.departure_load;

        // KIỂM TRA RÀNG BUỘC 1: Năng lượng
        // Nếu pin bị âm, tuyến đường không hợp lệ. Sử dụng sai số nhỏ để tránh lỗi dấu phẩy động.
        if (current_info.arrival_battery < -1e-6) {
            return false;
        }

        // Lấy các thuộc tính của điểm hiện tại (cửa sổ thời gian, thời gian phục vụ)
        double ready_time = 0, due_date = std::numeric_limits<double>::max(), service_time = 0;
        if (auto customer = std::dynamic_pointer_cast<Customer>(current_info.node)) {
            ready_time = customer->getReadyTime();
            due_date = customer->getDueDate();
            service_time = customer->getServiceTime();
        } else if (auto station = std::dynamic_pointer_cast<Station>(current_info.node)) {
            ready_time = station->getReadyTime();
            due_date = station->getDueDate();
        } else if (auto depot = std::dynamic_pointer_cast<Depot>(current_info.node)) {
            ready_time = depot->getReadyTime();
            due_date = depot->getLastTime();
        }

        // KIỂM TRA RÀNG BUỘC 2: Cửa sổ thời gian
        // Nếu đến muộn hơn thời gian cho phép, tuyến đường không hợp lệ.
        if (current_info.arrival_time > due_date + 1e-6) {
            return false;
        }

        // Tính thời gian chờ (nếu đến sớm hơn thời gian sẵn sàng)
        double wait_time = std::max(0.0, ready_time - current_info.arrival_time);
        
        // Xử lý logic tại điểm hiện tại để tính toán trạng thái "rời đi"
        if (auto customer = std::dynamic_pointer_cast<Customer>(current_info.node)) {
            // Tại khách hàng: phục vụ, giảm tải trọng, pin không đổi
            current_info.departure_time = current_info.arrival_time + wait_time + service_time;
            current_info.departure_load = current_info.arrival_load - customer->getDemand();
            current_info.departure_battery = current_info.arrival_battery;
        } else if (auto station = std::dynamic_pointer_cast<Station>(current_info.node)) {
            // Tại trạm sạc: sạc đầy pin
            // LOGIC SẠC PIN ĐÚNG: Dựa trên charging rate của trạm
            double battery_to_charge = instance->getVehicleBattery() - current_info.arrival_battery;
            double charging_time = (station->getChargingRate() > 0) ? (battery_to_charge / station->getChargingRate()) : 0;
            
            current_info.departure_time = current_info.arrival_time + wait_time + charging_time;
            current_info.departure_battery = instance->getVehicleBattery(); // Sạc đầy
            current_info.departure_load = current_info.arrival_load; // Tải trọng không đổi
        } else { // Tại Depot (điểm cuối)
            current_info.departure_time = current_info.arrival_time + wait_time;
            current_info.departure_battery = current_info.arrival_battery;
            current_info.departure_load = current_info.arrival_load;
        }
    }
    return true; // Nếu tất cả các điểm đều hợp lệ, trả về true
}

// --- CÁC PHƯƠNG THỨC ĐÃ ĐƯỢC TÁI CẤU TRÚC ---

void Route::recalculateFrom(size_t start_index) {
    // Hàm này giờ chỉ đơn giản là gọi logic lan truyền hợp nhất
    // trên chính tuyến đường của nó và vô hiệu hóa cache.
    if (propogateAndUpdate(this->infos, start_index)) {
        invalidateCache();
    } else {
        // Trường hợp này không nên xảy ra nếu các toán tử (ví dụ: evaluateInsertion)
        // đã kiểm tra tính hợp lệ trước khi chèn. Nó cho thấy một trạng thái không hợp lệ
        // đã được tạo ra. Tạm thời chỉ vô hiệu hóa cache.
        invalidateCache();
    }
}

void Route::insert(std::shared_ptr<Node> node, size_t position) {
    if (position > infos.size()) {
        position = infos.size();
    }
    RouteInfo newInfo;
    newInfo.node = node;

    infos.insert(infos.begin() + position, newInfo);
    
    // TỐI ƯU HÓA: Chỉ tính toán lại từ vị trí thay đổi trở đi.
    recalculateFrom(position);
}

void Route::remove(size_t position) {
    // Không thể xóa depot (điểm đầu và cuối)
    if (position > 0 && position < infos.size()) {
        infos.erase(infos.begin() + position);
        
        // TỐI ƯU HÓA: Tính toán lại từ vị trí vừa xóa.
        // Điểm tại `position` bây giờ là một điểm mới, cần cập nhật trạng thái của nó và các điểm sau đó.
        recalculateFrom(position);
    }
}

EvaluationResult Route::evaluateInsertion(std::shared_ptr<Node> node, size_t position) {
    EvaluationResult result;

    // KIỂM TRA NHANH RÀNG BUỘC 1: Tải trọng (nếu là khách hàng)
    if (auto customer_to_insert = std::dynamic_pointer_cast<Customer>(node)) {
        double current_total_demand = 0;
        for(const auto& info : infos) {
            if(auto c = std::dynamic_pointer_cast<Customer>(info.node)) {
                current_total_demand += c->getDemand();
            }
        }
        if (current_total_demand + customer_to_insert->getDemand() > vehicle->getCapacity() + 1e-6) {
            return result; // Trả về isFeasible = false
        }
    }

    // KIỂM TRA ĐẦY ĐỦ RÀNG BUỘC 2 & 3: Thời gian và Năng lượng
    double currentCost = this->getTotalTime(); // Sử dụng tổng thời gian làm chi phí

    // Bước 1: Tạo bản sao tạm thời của tuyến đường để giả lập việc chèn
    std::vector<RouteInfo> temp_infos = this->infos;
    
    // Bước 2: Chèn điểm mới vào bản sao
    RouteInfo newInfo;
    newInfo.node = node;
    temp_infos.insert(temp_infos.begin() + position, newInfo);

    // Bước 3: Lan truyền các cập nhật và kiểm tra tính khả thi trên bản sao
    if (propogateAndUpdate(temp_infos, position)) {
        // Bước 4: Nếu khả thi, tính toán chi phí chênh lệch
        result.isFeasible = true;
        double newCost = temp_infos.back().arrival_time; // Chi phí mới là thời gian đến điểm cuối cùng
        result.costDelta = newCost - currentCost;
    }
    // Bước 5: Nếu không khả thi, `result.isFeasible` vẫn là false

    return result;
}



bool Route::removeCustomer(std::shared_ptr<Customer> customer) {
    for (size_t i = 0; i < infos.size(); ++i) {
        if (infos[i].node == customer) {
            remove(i);
            return true;
        }
    }
    return false;
}

double Route::getCurrentLoad() const {
    if (infos.empty()) return 0;
    return infos.back().departure_load;
}

double Route::getCurrentBattery() const {
    if (infos.empty()) return 0;
    return infos.back().departure_battery;
}