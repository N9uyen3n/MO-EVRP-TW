#include "../../include/core/Solution.h"
#include "../../include/core/Instance.h"
#include "../../include/core/Customer.h"
#include <iostream>
#include <ostream>
#include <sstream>   // <-- Cần include thư viện này
#include <iomanip>
#include <set>       // Cần cho việc kiểm tra khách hàng trùng lặp

/**
 * @brief Khởi tạo một giải pháp, ban đầu chưa có tuyến đường nào.
 */
Solution::Solution(const std::shared_ptr<Instance>& instance)
    : instance(instance),
      totalVehicles(0),
      totalDistance(0.0),
      totalEnergy(0.0),       // (Hoặc totalChargeAmount)
      totalTime(0.0),
      maxTime(0.0),
      feasible(true)        // Một nghiệm rỗng luôn khả thi
{
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
    return this->totalDistance;
}

/**
 * @brief Tính tổng năng lượng tiêu thụ (tổng lượng đã sạc) của toàn bộ giải pháp.
 * (Giả định: getTotalEnergy() là tổng lượng sạc, dựa trên Route.h)
 */
double Solution::getTotalEnergy() const {
    return this->totalEnergy;
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

double Solution::getMaxTime() const {
    double total = 0.0;
    for (const auto& route : routes) {
        if (total < route.getTotalTime()) total = route.getTotalTime(); // Lấy từ evalResult của route
    }
    return total;
}

/**
 * @brief Hàm trả về số xe
 */
int Solution::getTotalVehicles() const {
    return this->totalVehicles;
}

/**
 * @brief Hàm công khai để kiểm tra tính khả thi.
 */
bool Solution::isFeasible() const {
    return checkGlobalFeasibility();
}

void Solution::evaluateRoutes() {
    // BƯỚC 1: Đánh giá từng tuyến (Giữ nguyên code của bạn)
    // Việc này đảm bảo mỗi 'route' tự tính toán (distance, time, feasible)
    // và cập nhật 'evalResult' của riêng nó.
    for (auto& route : this->routes) {
        route.evaluate();
    }

    // BƯỚC 2: (SỬA LỖI) Dọn dẹp các tuyến rỗng (0->0)
    // Lỗi này xảy ra khi Destroy/LocalSearch xóa hết khách hàng khỏi 1 tuyến
    //.
    routes.erase(
        std::remove_if(routes.begin(), routes.end(),
            [](const Route& route) {
                // Một tuyến hợp lệ phải có ít nhất 3 node (0 -> Customer -> 0)
                // Nếu nó chỉ có 2 (0 -> 0) hoặc ít hơn, xóa nó.
                return route.getNodes().size() <= 2;
            }
        ),
        routes.end()
    );

    // BƯỚC 3: (THÊM MỚI) Tổng hợp kết quả lên cấp Solution
    // (Đây là phần bị thiếu, rất quan trọng để ParetoArchive hoạt động đúng)

    // Reset tất cả mục tiêu về 0
    this->totalVehicles = routes.size(); // Đếm số xe SAU KHI DỌN DẸP
    this->totalDistance = 0.0;
    this->totalEnergy = 0.0;      // (Hoặc totalChargeAmount, tùy tên biến của bạn)
    this->totalTime = 0.0;        // (Tổng thời gian của TẤT CẢ các tuyến)
    this->maxTime = 0.0;          // (Thời gian của tuyến DÀI NHẤT)
    this->feasible = true;      // Giả định là khả thi

    // Lặp qua các tuyến "sạch" còn lại
    for (const auto& route : this->routes) {

        // Cộng dồn kết quả từ 'evalResult' của mỗi tuyến
        // (Giả sử bạn có các hàm getter này trong class Route)
        this->totalDistance += route.getTotalDistance();
        this->totalEnergy += route.getTotalChargeAmount();
        this->totalTime += route.getTotalTime();

        // Tìm thời gian của tuyến dài nhất (một mục tiêu quan trọng)
        if (route.getTotalTime() > this->maxTime) {
            this->maxTime = route.getTotalTime();
        }

        // Nếu MỘT tuyến không khả thi, CẢ nghiệm không khả thi
        if (!route.isFeasible()) {
            this->feasible = false;
        }
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

bool Solution::dominates(const Solution& other) const {
    bool betterInOne = false;

    // Mục tiêu 1: Tổng quãng đường
    if (this->getTotalDistance() > other.getTotalDistance()) return false; // Tệ hơn
    if (this->getTotalDistance() < other.getTotalDistance()) betterInOne = true;

    // Mục tiêu 2: Số lượng xe
    if (this->getTotalVehicles() > other.getTotalVehicles()) return false; // Tệ hơn
    if (this->getTotalVehicles() < other.getTotalVehicles()) betterInOne = true;

    // Mục tiêu 3: Thời gian tối đa
    if (this->getMaxTime() > other.getMaxTime()) return false; // Tệ hơn
    if (this->getMaxTime() < other.getMaxTime()) betterInOne = true;

    if (this->getTotalEnergy() > other.getTotalEnergy()) return false;
    if (this->getTotalEnergy() < other.getTotalEnergy()) betterInOne = true;


    return betterInOne; // Phải tốt hơn ít nhất 1 mục tiêu và không tệ hơn ở mục tiêu nào
}

void Solution::print() const {
    std::cout << "Total Vehicles: " << getTotalVehicles() << " Total Distance: " << getTotalDistance()
    << " Total Energy: " << getTotalEnergy() << " Total Time: " << getTotalTime() << " Max Time: " << getMaxTime() << std::endl;
    for (const auto& route : routes) {
        route.print();
        // std::cout << std::endl;
    }
}

std::string Solution::toString() const {
    // 1. Tạo một "stream" để xây dựng chuỗi
    std::stringstream ss;
    ss << std::fixed << std::setprecision(2); // Định dạng số float (ví dụ: 12.34)

    // 2. Gửi dữ liệu vào stringstream (giống như cout)
    ss << "Solution (Vehicles: " << getTotalVehicles()
       << ", Distance: " << getTotalDistance()
       << ", Energy: " << getTotalEnergy()
       << ", Time: " << getTotalTime()
       << ", MaxTime: " << getMaxTime()
       << ", Feasible: " << (isFeasible() ? "true" : "false") << ")\n";

    // 3. Lặp qua các tuyến và GỌI HÀM toString() CỦA CHÚNG
    for (const auto& route : routes) {
        // KHÔNG DÙNG route.print()
        ss << route.toString(); // Giả định Route::toString() cũng trả về std::string
    }

    // 4. Trả về chuỗi đã xây dựng
    return ss.str();
}