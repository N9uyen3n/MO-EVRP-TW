#pragma once
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

// --- Core Includes ---
#include "../core/Instance.h"
#include "../core/Solution.h"
#include "../core/Solver.h"


// --- ALNS Components ---
#include "IOperator.h"
#include "LocalSearch.h"
#include "ObjectiveEvaluator.h" // ⭐ NEW
#include "ParetoArchive.h"
#include "SolutionPool.h"

// [QUAN TRỌNG] Thêm bộ quản lý lịch sử cho các toán tử Learning
// #include "history/HistoryManager.h" // Tạm thời vô hiệu hóa

namespace logging {
class ILogger;
}

namespace alns {

/**
 * @brief Cấu hình chi tiết cho thuật toán ALNSSolver.
 */
struct ALNSConfig {
  // 1. Tham số Vòng lặp
  int maxIterations = 2500;
  int segmentIterations = 100;
  int maxIterationsWithoutImprovement = 1000;

  // 2. Tham số Thích ứng (Adaptive Weights)
  double decayParameter = 0.8;
  double scoreDominating = 35.0;
  double scoreNonDominated = 15.0;
  double scoreDominated = 5.0;
  double scoreIdentical = 0.0;

  // 3. Tham số Phá vỡ (Destroy)
  double minRemoval = 0.1;
  double maxRemoval = 0.4;

  // 4. Tham số Sửa chữa (Repair)
  int regretK = 3;
  double noiseParameter = 0.1;

  // 5. Tham số Tìm kiếm Cục bộ (Local Search)
  bool useLocalSearch = true;
  int localSearchIntensity = 30;

  // 6. Simulated Annealing
  double startTemperature = 150.0;
  double coolingRate = 0.995;
  double minTemperature = 0.5;

  bool enableLogging = false; // Mặc định là tắt

  // 7. Driver Equity / Fairness (NEW)
  double fairnessWeightStart = 0.1;
  double fairnessWeightEnd = 5.0;
  double fairnessTransitionRatio =
      0.3; // Transition starts after 30% iterations (tau)

  enum FairnessMode {
    COST_ONLY,        // w_F = 0
    STATIC_FAIRNESS,  // w_F = 1.0 (fixed)
    ADAPTIVE_FAIRNESS // w_F(t) adaptive
  };
  FairnessMode fairnessMode = ADAPTIVE_FAIRNESS;
};

// Lớp con trợ giúp để quản lý các toán tử và trọng số
struct OperatorPool {
  std::vector<std::shared_ptr<IOperator>> operators;
  std::vector<double> weights;
  std::vector<double> scores;
  std::vector<int> usages;

  // Chọn toán tử bằng Roulette Wheel
  int select(std::mt19937 &rng);
  // Cập nhật trọng số
  void updateWeights(double decay);
  // Reset điểm
  void resetScores();
};

class ALNSSolver : public Solver {
public:
  ALNSSolver(std::shared_ptr<Instance> instance, ALNSConfig config,
             const std::string &outputDirectory, const std::string &runName);

  ~ALNSSolver() override;

  // Hàm chính kế thừa từ Solver
  std::vector<Solution> solve() override;

  // Đăng ký toán tử
  void addDestroyOperator(std::shared_ptr<IDestroyOperator> op,
                          double initialWeight);
  void addRepairOperator(std::shared_ptr<IRepairOperator> op,
                         double initialWeight);

private:
  std::shared_ptr<Instance> instance;
  ALNSConfig config;

  std::mt19937 randomEngine; // Bộ tạo số ngẫu nhiên chính

  ParetoArchive archive;   // Kho lưu trữ đa mục tiêu
  LocalSearch localSearch; // Bộ tìm kiếm cục bộ
  SolutionPool solutionPool;
  ObjectiveEvaluator objEvaluator_; // ⭐ NEW
  // std::shared_ptr<HistoryManager> historyManager; // [MỚI] Bộ quản lý lịch sử
  // - Tạm thời vô hiệu hóa

  int totalCustomers;
  Solution s_current;
  double currentTemperature;
  double perturbationBoost_ =
      1.0; // Multiplier for destruction intensity during stagnation

  // Logger
  std::unique_ptr<logging::ILogger> logger;

  // Các "bể" toán tử
  OperatorPool destroyPool;
  OperatorPool repairPool;

  // Helper methods
  int calculateNodesToRemove();
  Solution generateInitialSolution();
};

} // namespace alns
