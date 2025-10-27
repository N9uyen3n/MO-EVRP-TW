/**
 * @file Route.h
 * @brief Định nghĩa lớp Route, quản lý một tuyến đường cho một xe.
 * Chịu trách nhiệm về việc sửa đổi tuyến đường, kiểm tra các ràng buộc và tính toán chi phí.
 */

#pragma once
#include <vector>
#include <memory>
#include "Vehicle.h"
#include "RouteInfo.h"
#include "Instance.h"
#include "Customer.h"

/**
 * @struct EvaluationResult
 * @brief Lưu kết quả của việc đánh giá một hành động chèn (insertion).
 * Được sử dụng bởi các toán tử trong ALNS để quyết định hành động tốt nhất.
 */
struct EvaluationResult {
    bool isFeasible = false; ///< Tuyến đường có khả thi (hợp lệ) sau khi chèn không.
    double costDelta = std::numeric_limits<double>::max(); ///< Mức thay đổi về chi phí (ví dụ: tổng thời gian) nếu việc chèn là khả thi.
};

/**
 * @class Route
 * @brief Đại diện cho một tuyến đường (lộ trình) của một xe.
 * 
 * Lớp này chứa một chuỗi các điểm (khách hàng, trạm sạc, depot) và quản lý
 * tất cả các trạng thái liên quan (thời gian, pin, tải trọng). Nó cung cấp các
 * phương thức để sửa đổi tuyến đường và đánh giá tính hợp lệ cũng như chi phí.
 */
class Route {
public:
    /**
     * @brief Hàm khởi tạo cho một tuyến đường.
     * @param id Mã định danh của tuyến đường.
     * @param vehicle Con trỏ đến xe sẽ thực hiện tuyến đường này.
     * @param instance Con trỏ đến dữ liệu của bài toán.
     */
    Route(int id, std::shared_ptr<Vehicle> vehicle, std::shared_ptr<const Instance> instance);
    ~Route() = default;

    // --- CÁC HÀM GETTER CƠ BẢN ---

    /** @brief Lấy ID của tuyến đường. */
    int getId() const;

    /** @brief Lấy con trỏ đến đối tượng xe được gán cho tuyến đường. */
    std::shared_ptr<Vehicle> getVehicle() const;

    /** @brief Lấy danh sách (không thể thay đổi) các thông tin của các điểm trên tuyến đường. */
    const std::vector<RouteInfo>& getInfos() const;

    // --- PHƯƠNG THỨC ĐÁNH GIÁ CHO ALNS ---

    /**
     * @brief Đánh giá chi phí và tính khả thi của việc chèn một điểm vào một vị trí.
     * Đây là hàm cốt lõi cho các toán tử "repair" trong ALNS.
     * @param node Điểm cần chèn (khách hàng hoặc trạm sạc).
     * @param position Vị trí (chỉ số) muốn chèn điểm vào.
     * @return Một đối tượng EvaluationResult chứa kết quả đánh giá.
     */
    EvaluationResult evaluateInsertion(std::shared_ptr<Node> node, size_t position);

    // --- CÁC PHƯƠNG THỨC SỬA ĐỔI TUYẾN ĐƯỜNG ---

    /**
     * @brief Chèn một điểm vào tuyến đường tại một vị trí.
     * Hàm này sẽ cập nhật lại trạng thái tuyến đường một cách hiệu quả từ vị trí chèn.
     * @param node Điểm cần chèn.
     * @param position Vị trí để chèn.
     */
    void insert(std::shared_ptr<Node> node, size_t position);

    /**
     * @brief Xóa một điểm khỏi tuyến đường tại một vị trí.
     * @param position Vị trí của điểm cần xóa.
     */
    void remove(size_t position);

    /**
     * @brief Tìm và xóa một khách hàng cụ thể khỏi tuyến đường.
     * @param customer Khách hàng cần xóa.
     * @return true nếu tìm thấy và xóa thành công, false nếu không.
     */
    bool removeCustomer(std::shared_ptr<Customer> customer);

    // --- CÁC HÀM GETTER CHO CÁC HÀM MỤC TIÊU ---

    /** @brief Lấy tổng khoảng cách của tuyến đường (sử dụng cache). */
    double getTotalDistance() const;

    /** @brief Lấy tổng thời gian của tuyến đường (sử dụng cache). Đây thường là hàm mục tiêu chính. */
    double getTotalTime() const;

    /** @brief Lấy tổng năng lượng đã sạc trên toàn tuyến đường (sử dụng cache). */
    double getTotalEnergyCharged() const;

    /** @brief Lấy số lượng khách hàng trên tuyến đường. */
    int getCustomerCount() const;

    // --- CÁC HÀM GETTER TIỆN ÍCH ---

    /** @brief Lấy tải trọng còn lại của xe tại điểm cuối cùng. */
    double getCurrentLoad() const;

    /** @brief Lấy mức pin còn lại của xe tại điểm cuối cùng. */
    double getCurrentBattery() const;

private:
    int id;
    std::shared_ptr<Vehicle> vehicle;
    std::vector<RouteInfo> infos;
    std::shared_ptr<const Instance> instance;

    // --- Bộ nhớ đệm (Cache) để tăng tốc độ truy vấn ---
    mutable double cachedTotalDistance;
    mutable double cachedTotalTime;
    mutable double cachedTotalEnergyCharged;
    mutable bool cacheValid;

    /** @brief Vô hiệu hóa cache, buộc phải tính toán lại trong lần gọi getter tiếp theo. */
    void invalidateCache();

    /** @brief Tính toán lại các giá trị tổng và lưu vào cache. */
    void rebuildCache() const;

    // --- LOGIC CỐT LÕI ĐỂ CẬP NHẬT TRẠNG THÁI TUYẾN ĐƯỜNG ---

    /**
     * @brief Lan truyền các thay đổi và cập nhật trạng thái (thời gian, pin, tải trọng) cho một chuỗi các điểm.
     * Đây là hàm logic hợp nhất, được sử dụng bởi cả việc sửa đổi thực tế và việc đánh giá giả lập.
     * @param route_infos Vector các điểm cần cập nhật (có thể là `this->infos` hoặc một bản sao tạm thời).
     * @param start_index Vị trí bắt đầu lan truyền cập nhật.
     * @return true nếu tuyến đường sau khi cập nhật vẫn hợp lệ, false nếu có vi phạm ràng buộc.
     */
    bool propogateAndUpdate(std::vector<RouteInfo>& route_infos, size_t start_index);

    /**
     * @brief Tính toán lại trạng thái của tuyến đường này bắt đầu từ một vị trí.
     * Hàm này gọi `propogateAndUpdate` trên `this->infos`.
     * @param start_index Vị trí bắt đầu tính toán lại.
     */
    void recalculateFrom(size_t start_index);
};
