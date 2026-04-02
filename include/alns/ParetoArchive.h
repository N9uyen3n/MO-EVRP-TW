// include/alns/ParetoArchive.h
#pragma once
#include "../core/Solution.h"
#include <memory>
#include <random>
#include <vector>

// Kết quả của việc thử thêm một nghiệm mới vào kho
enum class AddResult {
  DOMINATED,     // Bị loại (nghiệm mới tệ hơn)
  DOMINATING,    // Thêm vào (nghiệm mới tốt hơn và đã loại 1+ nghiệm cũ)
  NON_DOMINATED, // Thêm vào (nghiệm mới không bị thống trị)
  IDENTICAL      // Trùng lặp
};

class ParetoArchive {
public:
  explicit ParetoArchive(int maxSize);

  /**
   * @brief Hàm chấp nhận chính. Thử thêm một nghiệm mới vào kho.
   * Tự động xử lý việc loại bỏ các nghiệm bị thống trị.
   */
  AddResult tryAdd(const Solution &newSolution);

  /**
   * @brief Lấy một nghiệm từ kho để ALNS phá vỡ (destroy).
   */
  Solution getRandomSolution(std::mt19937 &rng) const;

  /**
   * @brief Trả về toàn bộ mặt trận Pareto (tập nghiệm cuối cùng).
   */
  const std::vector<Solution> &getFront() const;

  /**
   * @brief Lấy kích thước hiện tại của kho.
   */
  size_t getSize() const;

  /**
   * @brief Lấy giá trị mục tiêu tốt nhất (nhỏ nhất) cho từng mục tiêu
   * (Hữu ích cho việc log 'progress.csv')
   */
  Solution getBestSolutionForObjective(int objectiveIndex) const;

  /**
   * @brief Xóa tất cả các nghiệm trong kho.
   */
  void clear();

  /**
   * @brief Tính Hypervolume (3D) với Adaptive Normalization (Deb & Jain, 2014).
   *
   * Dùng 3 mục tiêu: TotalDistance, WorkloadGini, MaxTime.
   * Sử dụng thuật toán slicing: sắp xếp theo MaxTime, tại mỗi lát cắt
   * tính HV 2D của (Distance, Gini) và nhân với độ dày lát.
   * - Ideal point (z*): running minimum cho cả 3 chiều.
   * - Nadir point (z^nad): running maximum cho cả 3 chiều.
   * - Reference point: z_r = (1.1, 1.1, 1.1) sau normalize.
   *
   * @return Giá trị Hypervolume đã chuẩn hóa. Max lý thuyết = 1.1^3 = 1.331.
   */
  double computeHypervolume() const;
  double computeHypervolume2D() const;

  /**
   * @brief Initializes the fixed reference box using the initial solution.
   * Ensures consistent HV scaling over the entire run.
   */
  void initializeReferenceBox(const Solution &initialSolution);

  /**
   * @brief Expands the nadir box if the provided solution comes close to or
   * exceeds it.
   */
  void expandNadirIfNeeded(const Solution &sol);

private:
  // Sorts by number of vehicles (ascending).
  // For each vehicle count, stores a 2D Pareto Front of (Distance,
  // WorkloadGini)
  std::map<int, std::vector<Solution>> archiveByVehicles_;
  int maxSize; // maxSize PER VEHICLE LEVEL

  // Fixed Reference Box for HV to prevent artificial scaling
  double refIdealDist_ = 0.0;
  double refIdealGini_ = 0.0;
  double refIdealMaxTime_ = 0.0;

  double refNadirDist_ = 1e6;
  double refNadirGini_ = 1.0;
  double refNadirMaxTime_ = 1e6;

  // Cache for returning the flat front
  mutable std::vector<Solution> flatArchiveCache_;

  /**
   * @brief Cắt tỉa kho nếu vượt quá 'maxSize'.
   * Dùng "Crowding Distance" (từ NSGA-II) để loại bỏ nghiệm.
   */
  void prune(int veh);
};