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

private:
  // Sorts by number of vehicles (ascending).
  // For each vehicle count, stores a 2D Pareto Front of (Distance,
  // WorkloadGini)
  std::map<int, std::vector<Solution>> archiveByVehicles_;
  int maxSize; // maxSize PER VEHICLE LEVEL

  // Fixed Reference Box for HV to prevent artificial scaling
  mutable double refIdealDist_ = 1e18;
  mutable double refIdealGini_ = 1e18;
  mutable double refIdealMaxTime_ = 1e18;

  mutable double refNadirDist_ = -1e18;
  mutable double refNadirGini_ = -1e18;
  mutable double refNadirMaxTime_ = -1e18;

  mutable int currentMinVehicles_ = 1e9; // Trigger for reference box reset

  // Cache for returning the flat front
  mutable std::vector<Solution> flatArchiveCache_;

  /**
   * @brief Cắt tỉa kho nếu vượt quá 'maxSize'.
   * Dùng "Crowding Distance" (từ NSGA-II) để loại bỏ nghiệm.
   */
  void prune(int veh);

  /**
   * @brief Cập nhật hệ trục tọa độ tham chiếu cho biểu đồ HV.
   * CHỈ CẬP NHẬT khi kho thiết lập kỷ lục mới về số xe tối thiểu (Min
   * Vehicles). Điều này cô lập hộp chuẩn hóa, đảm bảo HV tăng thuần túy do chất
   * lượng nghiệm, không bị nhiễu do giãn nở không gian chuẩn hóa.
   */
  void updateReferencePoints() const;
};