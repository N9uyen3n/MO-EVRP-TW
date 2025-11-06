#include "Solution.h"

#include <iostream>
#include <ostream>

#include "Instance.h"
#include "Customer.h" // Cần để kiểm tra node có phải là Customer không
#include <set>       // Cần cho việc kiểm tra khách hàng trùng lặp

/**
 * @brief Khởi tạo một giải pháp, ban đầu chưa có tuyến đường nào.
 */
Solution::Solution(const std::shared_ptr<Instance>& instance)
    : instance(instance) {
    // routes vector được tự động khởi tạo rỗng
}

/**
 * @brief Thêm một tuyến đường (đã được tạo) vào giải pháp.
 */
void Solution::addRoute(const Route& route) {
    routes.push_back(route);
}


/**
 * @brief Xóa một tuyến đường khỏi giải pháp dựa trên chỉ số (index).
 */
void Solution::removeRoute(size_t index) {
    if (index >= routes.size()) {
        throw std::out_of_range("Chỉ số tuyến đường không hợp lệ.");
    }
    routes.erase(routes.begin() + index);
}

/**
 * @brief Xóa tất cả các tuyến đường khỏi giải pháp.
 */
void Solution::clear() {
    routes.clear();
}

/**
 * @brief Lấy danh sách (const) tất cả các tuyến đường.
 */
const std::vector<Route>& Solution::getRoutes() const {
    return routes;
}

std::vector<Route>& Solution::getRoutes() { // Có thể thay đổi
    return routes;
}
/**
 * @brief Tính tổng quãng đường của toàn bộ giải pháp.
 */
double Solution::getTotalDistance() const {
    double total = 0.0;
    for (const auto& route : routes) {
        total += route.getTotalDistance(); // Lấy từ evalResult của route
    }
    return total;
}

/**
 * @brief Tính tổng năng lượng tiêu thụ (tổng lượng đã sạc) của toàn bộ giải pháp.
 * (Giả định: getTotalEnergy() là tổng lượng sạc, dựa trên Route.h)
 */
double Solution::getTotalEnergy() const {
    double total = 0.0;
    for (const auto& route : routes) {
        total += route.getTotalChargeAmount(); // Lấy từ evalResult của route
    }
    return total;
}

/**
 * @brief Tính tổng thời gian (làm việc của tài xế) của toàn bộ giải pháp.
 */
double Solution::getTotalTime() const {
    double total = 0.0;
    for (const auto& route : routes) {
        total += route.getTotalTime(); // Lấy từ evalResult của route
    }
    return total;
}
/**
 * @brief Hàm trả về số xe
 */
int Solution::getTotalVehicles() const {
    return routes.size();
}

/**
 * @brief Hàm công khai để kiểm tra tính khả thi.
 */
bool Solution::isFeasible() const {
    return checkGlobalFeasibility();
}

void Solution::evaluateRoutes() {
    // Lặp qua từng tuyến đường trong danh sách
    for (auto& route : this->routes) {
        route.evaluate();
    }
}

/**
 * @brief Hàm nội bộ kiểm tra tính khả thi TOÀN CỤC.
 * 1. Mọi tuyến con phải khả thi.
 * 2. Mọi khách hàng phải được phục vụ ĐÚNG 1 LẦN.
 */
bool Solution::checkGlobalFeasibility() const {
    
    // Lấy danh sách ID của tất cả khách hàng có trong bài toán
    std::set<int> all_customers_in_instance;
    for (const auto& node : instance->getNodes()) {
        // Kiểm tra xem node này có phải là Customer không
        if (std::dynamic_pointer_cast<Customer>(node)) {
            all_customers_in_instance.insert(node->getId());
        }
    }

    std::set<int> customers_served;

    for (const Route& route : this->routes) {
        
        // 1. Kiểm tra tính khả thi của từng tuyến (Cấp độ Vi mô)
        // Hàm route.isFeasible() đã kiểm tra (pin, tải, thời gian)
        if (!route.isFeasible()) {
            return false; // Một tuyến không khả thi -> Toàn bộ giải pháp không khả thi
        }

        // 2. Thu thập khách hàng đã phục vụ trong tuyến này
        for (int node_id : route.getNodes()) {
            // Kiểm tra xem node_id này có phải là 1 khách hàng trong bài toán không
            if (all_customers_in_instance.count(node_id)) {
                
                // Kiểm tra xem khách hàng này đã được phục vụ ở tuyến khác chưa
                if (customers_served.count(node_id)) {
                    // Lỗi: Khách hàng được phục vụ > 1 lần
                    return false; 
                }
                customers_served.insert(node_id);
            }
        }
    }

    // 3. Kiểm tra cuối cùng:
    // Số khách hàng đã phục vụ có bằng số khách hàng trong bài toán không?
    return customers_served.size() == all_customers_in_instance.size();
}

void Solution::toString() const {
    for (const auto& route : routes) {
        route.toString();
        // std::cout << std::endl;
    }
}