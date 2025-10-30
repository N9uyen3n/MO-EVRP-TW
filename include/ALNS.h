#ifndef ALNS_H
#define ALNS_H

#include "Solver.h"
#include "Solution.h"
#include "Instance.h"
#include "Operators.h" // <-- BAO GỒM FILE TOÁN TỬ MỚI
#include <random>
#include <vector>
#include <map>
#include <string>
#include <memory> // Cho std::shared_ptr
#include <set>    // Cho allCustomerIds

// Cấu trúc để lưu trữ thông tin về một toán tử
struct OperatorStats {
    double weight; // Trọng số cho roulette wheel
    double score;  // Điểm tích lũy trong 1 segment
    int timesUsed; // Số lần đã dùng trong 1 segment

    OperatorStats() : weight(1.0), score(0.0), timesUsed(0) {}
};

struct ALNSParameters {
    // --- Tham số ALNS chính ---
    int maxIterations = 25000;
    // Tham số SA
    double coolingRate = 0.99975;
    double initialTempControl = 0.05;
    // Tham số điểm (scores)
    double score_newBest = 33;
    double score_better = 20;
    double score_accepted = 13;
    // Tham số cập nhật trọng số
    double reactionFactor = 0.1;
    int segmentIterations_NC = 250;
    int segmentIterations_NS = 2000;
    // Tham số Route/Station Removal
    int iterations_NRR = 5000;
    int consecutive_nRR = 1000;
    int iterations_NSR = 60;

    // --- Tham số cho các toán tử ---
    // ShawRemoval
    double shaw_p1 = 9.0;
    double shaw_p2 = 13.0;
    double shaw_p3 = 2.0;
    double shaw_p4 = 5.0;
    double shaw_eta = 6.0;
    // RegretKInsertion
    int regret_k_val = 3;
    // WorstDistanceRemoval
    double worst_dist_kappa = 5.0;
};

class ALNS : public Solver {
public:
    ALNS(std::shared_ptr<Instance> instance, std::mt19937& rng);
    ALNS(std::shared_ptr<Instance> instance, std::mt19937& rng,
         // Tham số ALNS
         int maxIterations, double coolingRate, double initialTempControl,
         double score_newBest, double score_better, double score_accepted,
         double reactionFactor, int segmentIterations_NC, int segmentIterations_NS,
         int iterations_NRR, int consecutive_nRR, int iterations_NSR);
    std::vector<Solution> solve() override;

private:
    // --- Cấu trúc dữ liệu ALNS ---
    std::shared_ptr<Instance> instance;
    std::mt19937& rng;
    std::vector<int> unservedCustomers; // "Bank"
    std::set<int> allCustomerIds;

    // --- Trạng thái Thuật toán ---
    Solution bestSolution;
    Solution currentSolution;
    double temperature;

    // --- Tham số ALNS (Chỉ giữ lại các tham số của ALNS) ---
    // [cite_start]// [cite: 634]
    int maxIterations;
    double coolingRate;     // (epsilon)
    double initialTempControl; // (mu)
    double score_newBest;      // (sigma1)
    double score_better;       // (sigma2)
    double score_accepted;     // (sigma3)
    double reactionFactor;  // (rho)
    int segmentIterations_NC; // (Nc)
    int segmentIterations_NS; // (Ns)
    int iterations_NRR; // (N_RR)
    int consecutive_nRR; // (n_RR)
    int iterations_NSR; // (N_SR)

    // (Đã xóa các tham số Shaw và Regret)

    // --- Quản lý Toán tử (ĐÃ THAY ĐỔI) ---
    // Chúng ta lưu trữ các cặp <Tên, Cặp<Đối tượng toán tử, Chỉ số Thống kê>>
    using CustomerDestroyMap = std::map<std::string, std::pair<std::shared_ptr<ICustomerDestroy>, OperatorStats>>;
    using CustomerRepairMap  = std::map<std::string, std::pair<std::shared_ptr<ICustomerRepair>, OperatorStats>>;
    using StationDestroyMap  = std::map<std::string, std::pair<std::shared_ptr<IStationDestroy>, OperatorStats>>;
    using StationRepairMap   = std::map<std::string, std::pair<std::shared_ptr<IStationRepair>, OperatorStats>>;

    CustomerDestroyMap destroyOps_Customer;
    CustomerRepairMap  repairOps_Customer;
    StationDestroyMap  destroyOps_Station;
    StationRepairMap   repairOps_Station;

    // --- 1. Khởi tạo ---
    Solution createInitialSolution();

    // (Đã xóa tất cả các khai báo hàm toán tử cũ: randomRemoval, greedyInsertion, v.v...)

    // Chọn các toán tử phá hủy và sửa chữa
    std::pair<std::string, std::shared_ptr<ICustomerDestroy>> selectCustomerDestroyOperator();
    std::pair<std::string, std::shared_ptr<ICustomerRepair>>  selectCustomerRepairOperator();
    std::pair<std::string, std::shared_ptr<IStationDestroy>>  selectStationDestroyOperator();
    std::pair<std::string, std::shared_ptr<IStationRepair>>   selectStationRepairOperator();

    int compareSolutions(const Solution& sol1, const Solution& sol2) const;

    // Dùng template để cập nhật cả 4 loại map
    template<typename OpMap>
    void updateScores(OpMap& operatorMap, const std::string& opName, int scoreType);

    template<typename OpMap>
    void updateWeights(OpMap& operatorMap);

    // --- 6. Helpers ---
    int getNumToRemove(int numCustomers);
    // (Đã xóa các helper của toán tử - di chuyển sang Operators.h/cpp)
};

#endif // ALNS_H