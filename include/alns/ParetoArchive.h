// include/alns/ParetoArchive.h
#pragma once
#include "core/Solution.h"
#include <vector>
#include <memory>
#include <random>

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
    AddResult tryAdd(const Solution& newSolution);

    /**
     * @brief Lấy một nghiệm từ kho để ALNS phá vỡ (destroy).
     */
    Solution getRandomSolution(std::mt19937& rng) const;

    /**
     * @brief Trả về toàn bộ mặt trận Pareto (tập nghiệm cuối cùng).
     */
    const std::vector<Solution>& getFront() const;

    /**
     * @brief Lấy kích thước hiện tại của kho.
     */
    size_t getSize() const;

    /**
     * @brief Lấy giá trị mục tiêu tốt nhất (nhỏ nhất) cho từng mục tiêu
     * (Hữu ích cho việc log 'progress.csv')
     */
    Solution getBestSolutionForObjective(int objectiveIndex) const;


private:
    std::vector<Solution> archive; // Danh sách các nghiệm không bị thống trị
    int maxSize;

    /**
     * @brief Cắt tỉa kho nếu vượt quá 'maxSize'.
     * Dùng "Crowding Distance" (từ NSGA-II) để loại bỏ nghiệm.
     */
    void prune();
};