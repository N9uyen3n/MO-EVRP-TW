#pragma once
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>
#include <climits>

// --- Core Includes ---
#include "../core/Instance.h"
#include "../core/Solution.h"
#include "../core/Solver.h"

// --- ALNS Components ---
#include "IOperator.h"
#include "LocalSearch.h"
#include "ParetoArchive.h"
#include "ScatterSearch.h"
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
  int maxIterations = 25000;
  int segmentIterations = 100;

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
  double SAScale = 10.0;

  long long maxTime = 1800000; // ms - 30 phut

  // 7. HV-Based Convergence (Dừng sớm dựa trên Hypervolume)
  double hvImprovementThreshold = 0.001; // ε = 0.1% cải thiện tối thiểu
  int hvStagnationLimit = 5;

  bool trigger = false;
  // Dừng sau N segment không cải thiện

  // 8. Scatter Search Intensification
  bool useScatterSearch = false;
  ScatterSearch::Config
      scatterSearchConfig; // Dùng mặc định của ScatterSearch::Config

  bool enableLogging = false;  // Mặc định là tắt
  unsigned int randomSeed = 0; // 0 means use random_device (time-based)

// 9. Penalty Relaxation for Constraint Violation
double lambdaTW = 10.0;       // Initial TW penalty coefficient
double lambdaTW_max = 500.0;  // Maximum TW penalty coefficient (increased from 100)
double lambdaPin = 5.0;       // Initial battery penalty coefficient
double lambdaPin_max = 200.0; // Maximum battery penalty coefficient (increased from 50)
bool usePenaltyRelaxation = true; // Enable penalty relaxation mechanism
};

// Lớp con trợ giúp để quản lý các toán tử và trọng số
struct OperatorPool {
  std::vector<std::shared_ptr<IOperator>> operators;
  std::vector<double> weights;
  std::vector<double> scores;
  std::vector<int> usages;
  std::vector<double> initialWeights;

  // Chọn toán tử bằng Roulette Wheel
  int select(std::mt19937 &rng);
  // Cập nhật trọng số
  void updateWeights(double decay);
  // Reset điểm
  void resetScores();
};

// Diversity tracking for destroy operators
struct DestroyOperatorDiversity {
 // Track last N operator types used
 std::vector<DestroyOperatorType> recentTypes;
 const int DIVERSITY_WINDOW = 5;

 // Track usage per segment
 std::map<DestroyOperatorType, int> segmentUsage;

 // Penalty factor per recent use (20% penalty)
 const double TYPE_PENALTY = 0.2;

 void recordUsage(DestroyOperatorType type) {
  recentTypes.push_back(type);
  if (static_cast<int>(recentTypes.size()) > DIVERSITY_WINDOW) {
   recentTypes.erase(recentTypes.begin());
  }
 }

 void resetSegment() {
  segmentUsage.clear();
 }

 void incrementSegmentUsage(DestroyOperatorType type) {
  segmentUsage[type]++;
 }

 int getSegmentUsage(DestroyOperatorType type) const {
  auto it = segmentUsage.find(type);
  return (it != segmentUsage.end()) ? it->second : 0;
 }
};

class ALNSSolver : public Solver {
public:
  ALNSSolver(std::shared_ptr<Instance> instance, ALNSConfig config,
             const std::string &outputDirectory, const std::string &runName);

  ~ALNSSolver() override;

  // Hàm chính kế thừa từ Solver
  std::vector<Solution> solve() override;

  // Chạy ALNS ngắn để cải thiện một nghiệm cụ thể (dùng bởi ScatterSearch)
  void improveSolution(Solution &sol, int maxIters);

  // Đăng ký toán tử
  void addDestroyOperator(std::shared_ptr<IDestroyOperator> op,
                          double initialWeight);
  void addRepairOperator(std::shared_ptr<IRepairOperator> op,
                         double initialWeight);
  double getHV();
private:
  std::shared_ptr<Instance> instance;
  ALNSConfig config;

  std::mt19937 randomEngine; // Bộ tạo số ngẫu nhiên chính

  ParetoArchive archive;     // Kho lưu trữ đa mục tiêu
  LocalSearch localSearch;   // Bộ tìm kiếm cục bộ
  SolutionPool solutionPool; // <-- THÊM VÀO
  // std::shared_ptr<HistoryManager> historyManager; // [MỚI] Bộ quản lý lịch sử
  // - Tạm thời vô hiệu hóa

  int totalCustomers;
    // int frvLastTriggerIter_;
  Solution s_current;
  double currentTemperature;
  double perturbationBoost_ =
      1.0; // Multiplier for destruction intensity during stagnation
  std::string runName_; // Store instance name for type detection

  // HV-Based Early Stopping state
  double previousHV_ = 0.0;
  int hvStagnationCount_ = 0;

  // Scatter Search intensification
  std::unique_ptr<ScatterSearch> scatterSearch_;

  // Logger
  std::unique_ptr<logging::ILogger> logger;

  // [OPT-5] MIN_WEIGHT floor + VR Mode boost
  static constexpr double MIN_WEIGHT = 0.5; // Minimum weight floor
  bool vrModeActive_ = false;          // Vehicle reduction mode
  int vrModeStartIter_ = 20000;        // Start VR mode at iteration 15000
  int vrModeMinWeight_ = 5;            // Minimum weight for VR operators

  // Vehicle-specific stagnation tracking (BKS+1 problem)
  int lastBestVehicles_ = INT_MAX;     // Best vehicle count found
  int lastVehicleImprovementIter_ = 0; // Last iteration vehicles improved
  int consecutiveVehicleReductionFailures_ = 0; // Failed VR attempts

// Penalty Relaxation state
double currentLambdaTW_ = 10.0;  // Current TW penalty coefficient
double currentLambdaPin_ = 5.0;  // Current battery penalty coefficient
int penaltyRelaxationStartIter_ = 0; // Start from iteration 0

  // Operator pools
  OperatorPool destroyPool;
  OperatorPool repairPool;

  // Diversity tracking for destroy operators
  DestroyOperatorDiversity destroyDiversity;

  // Helper methods
  int calculateNodesToRemove();
  std::vector<Solution> generateInitialSolution();

  // Multi-start construction helpers
  Solution
  constructSolutionFromOrder(const std::vector<int> &orderedCustomerIds);
  std::vector<int> generateSweepOrder();
  std::vector<int> generateNNOrder();
  std::vector<int> generateEDFOrder();
  std::vector<int> generateTightestTWOrder();
  std::vector<int> generateRandomizedWeightedOrder(double wDist, double wTW,
                                                   double wDemand);
};

} // namespace alns
