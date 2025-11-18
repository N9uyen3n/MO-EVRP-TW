// include/alns/ALNSSolver.h
#pragma once
#include "core/Solver.h"
#include "core/Instance.h"
#include "core/Solution.h"
#include "IOperator.h"
#include "ParetoArchive.h"
#include "LocalSearch.h"
#include <vector>
#include <memory>
#include <map>
#include <random>
#include <string>

// --- Forward Declaration (để tránh circular dependency) ---
namespace logging {
    class ILogger; // <-- Trỏ đến GIAO DIỆN, không trỏ đến Logger cụ thể
}
/**
 * @brief Cấu hình chi tiết cho thuật toán ALNSSolver.
 */
struct ALNSConfig {
    // --- 1. Tham số Vòng lặp ---
    int maxIterations;
    int segmentIterations;
    int maxIterationsWithoutImprovement;

    // --- 2. Tham số Thích ứng (Adaptive Weights) ---
    double decayParameter;           // (rho)
    double scoreDominating;          // (sigma_1)
    double scoreNonDominated;        // (sigma_2)
    double scoreDominated;           // (sigma_3)
    double scoreIdentical;           // (sigma_4)

    // --- 3. Tham số Phá vỡ (Destroy) ---
    // (Dùng % để linh hoạt với các kích cỡ input)
    double minRemoval;     // Tỷ lệ xóa tối thiểu (ví dụ: 0.1 = 10%)
    double maxRemoval;     // Tỷ lệ xóa tối đa (ví dụ: 0.4 = 40%)

    // --- 4. Tham số Sửa chữa (Repair) ---
    /**
     * @brief Tham số k cho Regret-k.
     * Đây chính là "nRR" mà bạn hỏi.
     */
    int regretK;

    /**
     * @brief Tham số nhiễu (Noise).
     * Đây chính là "NRR" mà bạn hỏi.
     * (Giá trị thường nằm trong khoảng [0.0, 1.0])
     */
    double noiseParameter;

    // --- 5. Tham số Tìm kiếm Cục bộ (Local Search) ---
    bool useLocalSearch;
    int localSearchIntensity;

    double startTemperature;    // Nhiệt độ ban đầu
    double coolingRate;         // Tỷ lệ làm nguội (ví dụ: 0.995)
    double minTemperature;      // Nhiệt độ tối thiểu

    // Tùy chọn in log
    bool enableLogging = true;
};


// Lớp con trợ giúp để quản lý các toán tử và trọng số
struct OperatorPool {
    std::vector<std::shared_ptr<IOperator>> operators;
    std::vector<double> weights;
    std::vector<double> scores;
    std::vector<int> usages;

    // Hàm chọn toán tử bằng Roulette Wheel
    // IOperator& select(std::mt19937& rng);
    int select(std::mt19937& rng);
    // Cập nhật trọng số dựa trên điểm số
    void updateWeights(double decay);
    // Reset điểm và số lần dùng (gọi sau mỗi segment)
    void resetScores();
};


class ALNSSolver : public Solver {
public:
    ALNSSolver(std::shared_ptr<Instance> instance, ALNSConfig config, const std::string& runID);
    ALNSSolver(std::shared_ptr<Instance> instance, ALNSConfig config,
           const std::string& outputDirectory,
           const std::string& runName);
    ~ALNSSolver() override; // Phải định nghĩa trong .cpp vì có unique_ptr

    // Hàm chính kế thừa từ Solver
    std::vector<Solution> solve() override;

    // Dùng để đăng ký các toán tử
    void addDestroyOperator(std::shared_ptr<IDestroyOperator> op, double initialWeight);
    void addRepairOperator(std::shared_ptr<IRepairOperator> op, double initialWeight);

private:
    std::shared_ptr<Instance> instance;
    ALNSConfig config;
    std::mt19937 rng; // Bộ tạo số ngẫu nhiên

    ParetoArchive archive;    // Kho lưu trữ đa mục tiêu
    LocalSearch localSearch;  // Bộ tìm kiếm cục bộ (SMD)
    int totalCustomers;

    Solution s_current;

    int calculateNodesToRemove();

    double currentTemperature; // Nhiệt độ hiện tại
    std::uniform_real_distribution<double> dist_0_1; // Để tạo số ngẫu nhiên [0, 1]

    // Logger để ghi file phân tích
    std::unique_ptr<logging::ILogger> logger;

    // Các "bể" toán tử
    OperatorPool destroyPool;
    OperatorPool repairPool;

    std::mt19937 randomEngine;

    // --- Các hàm trong vòng lặp ---
    Solution generateInitialSolution();
};