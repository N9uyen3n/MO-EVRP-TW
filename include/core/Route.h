#pragma once
#include <vector>
#include <memory>
#include <stdexcept>
#include "Node.h"
#include "Instance.h"
#include "Vehicle.h"

// Trạng thái của xe tại mỗi node
struct NodeState {
    double arrivalTime;     // Thời gian đến
    double departureTime;   // Thời gian đi (sau khi phục vụ/sạc)
    double timeWait;
    double remainingBattery; // Lượng pin còn lại khi đến
    double remainingLoad;    // Lượng tải còn lại sau khi phục vụ
    double chargeAmount;    // Lượng pin đã sạc tại node này (nếu là trạm sạc)
};

// Kết quả đánh giá của toàn bộ tuyến đường
struct EvaluationResult {
    double totalDistance;     // Tổng quãng đường
    double totalTime;         // Tổng thời gian tài xế làm việc
    double totalWaitTime;     // Thời gian chờ (thời gian mà phục vụ khách hàng)
    double totalChargeTime;   // Thời gian sạc (thời gian ở trạm sạc)
    double totalChargeAmount; // Tổng lượng pin đã sạc
    double totalEnergyConsumption; // Tổng năng lượng tiêu thụ
    bool feasible;            // Ràng buộc (năng lượng, thời gian, tải)

    // Hàm khởi tạo mặc định (đã cập nhật)
    EvaluationResult(double dist = 0.0, double time = 0.0, double wait = 0.0, double chargeT = 0.0,
                     double chargeA = 0.0, double energyC = 0.0, bool feas = true)
        : totalDistance(dist), totalTime(time),totalWaitTime(wait), totalChargeTime(chargeT),
          totalChargeAmount(chargeA), totalEnergyConsumption(energyC), feasible(feas) {}
};

struct InsertionResult {
    bool isFeasible = false;
    // Delta (thay đổi) của các mục tiêu chính
    double deltaDistance = 0.0;
    double deltaChargeAmount = 0.0;
    double deltaWaitTime = 0.0;
    double deltaEnergyConsumption = 0.0;
};

class Route {
    public:
        Route(int id, std::shared_ptr<Vehicle> vehicle, const std::shared_ptr<Instance>& instance);

        int getId() const;
        // --- Modifications ---
        void addNode(int nodeId, size_t position); // thêm node vào vị trí tùy ý
        void addNode(int nodeId); // thêm node vào vị trí cuối cùng
        void removeNode(size_t position);           // xóa node ở vị trí nào đó
        void clear();                               // xóa hết tuyến
        void reverseNodes(size_t i, size_t j);

        // --- Getters (Đã cập nhật) ---
        const std::vector<int>& getNodes() const;
        bool isFeasible() const;

        // --- Getters cho các hàm mục tiêu (Objectives) ---
        double getTotalDistance() const;
        double getTotalWaitTime() const;
        double getTotalChargeTime() const;
        double getTotalChargeAmount() const;
        double getTotalEnergyConsumption() const;

        // --- Getters cho thông tin nội bộ (Internal info) ---
        double getTotalTime() const;
        // --- Evaluation ---
        void evaluate(); // tính toán lại toàn bộ chi phí tuyến

        const std::vector<NodeState>& getStates() const;
        int getNodeAt(size_t pos) const;
        size_t size() const;

        // --- Utilities ---
    void print() const;
    std::string toString() const;
    long long getHash() const;




        InsertionResult checkInsertionCost(int customerId, size_t position) const;

    private:
        int id;
        std::shared_ptr<Vehicle> vehicle;
        std::shared_ptr<Instance> instance;  // dữ liệu đầu vào toàn cục
        std::vector<int> nodeSequence;       // chuỗi ID: depot → ... → depot
        std::vector<NodeState> states;      // Trạng thái của từng node trong route
        EvaluationResult evalResult;         // kết quả đánh giá

};