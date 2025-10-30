#pragma once
#include <vector>
#include <memory>
#include <stdexcept>
#include "Node.h"
#include "Instance.h"
#include "Vehicle.h"

// Trạng thái của xe tại mỗi node
struct RouteState {
    double arrivalTime;     // Thời gian đến
    double departureTime;   // Thời gian đi (sau khi phục vụ/sạc)
    double remainingBattery; // Lượng pin còn lại khi đến
    double remainingLoad;    // Lượng tải còn lại sau khi phục vụ
    double chargeAmount;    // Lượng pin đã sạc tại node này (nếu là trạm sạc)
};

// Kết quả đánh giá của toàn bộ tuyến đường
// Đã cập nhật để chứa các mục tiêu từ MOEVRP.pdf
struct EvaluationResult {
    double totalDistance;     // Tổng quãng đường
    double totalTime;         // Tổng thời gian tài xế làm việc
    double totalWaitTime;     // Thời gian chờ (thời gian mà phục vụ khách hàng)
    double totalChargeTime;   // Thời gian sạc (thời gian ở trạm sạc)
    double totalChargeAmount; // Tổng lượng pin đã sạc
    bool feasible;            // Ràng buộc (năng lượng, thời gian, tải)

    // Hàm khởi tạo mặc định (đã cập nhật)
    EvaluationResult(double dist = 0.0, double time = 0.0, double wait = 0.0, double chargeT = 0.0,
                     double chargeA = 0.0, bool feas = true)
        : totalDistance(dist), totalTime(time),totalWaitTime(wait), totalChargeTime(chargeT),
          totalChargeAmount(chargeA), feasible(feas) {}
};

class Route {
    public:
        Route(int id, std::shared_ptr<Vehicle> vehicle, const std::shared_ptr<Instance>& instance);

        int getId() const;
        // --- Modifications ---
        void addNode(int nodeId, size_t position); // thêm node vào vị trí tùy ý
        void removeNode(size_t position);           // xóa node ở vị trí nào đó
        void clear();                               // xóa hết tuyến

        // --- Getters (Đã cập nhật) ---
        const std::vector<int>& getNodes() const;
        bool isFeasible() const;

        // --- Getters cho các hàm mục tiêu (Objectives) ---
        double getTotalDistance() const;
        double getTotalWaitTime() const;
        double getTotalChargeTime() const;
        double getTotalChargeAmount() const;

        // --- Getters cho thông tin nội bộ (Internal info) ---
        double getTotalTime() const;
        // --- Evaluation ---
        void evaluate(); // tính toán lại toàn bộ chi phí tuyến

    private:
        int id;
        std::shared_ptr<Vehicle> vehicle;
        std::shared_ptr<Instance> instance;  // dữ liệu đầu vào toàn cục
        std::vector<int> nodeSequence;       // chuỗi ID: depot → ... → depot
        std::vector<RouteState> states;      // Trạng thái của từng node trong route
        EvaluationResult evalResult;         // kết quả đánh giá

};