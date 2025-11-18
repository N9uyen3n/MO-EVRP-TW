#include "alns/ParetoArchive.h"
#include <algorithm>
#include <limits>
#include <numeric>   // Cho std::iota
#include <stdexcept>
#include <cmath>     // Cho std::abs

ParetoArchive::ParetoArchive(int maxSize) : maxSize(maxSize) {
    if (maxSize <= 0) {
        throw std::invalid_argument("ParetoArchive max size must be positive.");
    }
}

size_t ParetoArchive::getSize() const {
    return archive.size();
}

const std::vector<Solution>& ParetoArchive::getFront() const {
    return archive;
}

Solution ParetoArchive::getRandomSolution(std::mt19937& rng) const {
    if (archive.empty()) {
        throw std::runtime_error("Cannot get random solution: Pareto archive is empty.");
    }
    std::uniform_int_distribution<> dist(0, archive.size() - 1);
    return archive[dist(rng)];
}

AddResult ParetoArchive::tryAdd(const Solution& newSolution) {
    bool isDominated = false;
    std::vector<int> dominatedIndices;
    const double EPSILON = 1e-9; // Dùng cho so sánh số thực

    for (size_t i = 0; i < archive.size(); ++i) {
        const auto& existingSolution = archive[i];

        // =====================================================================
        // *** SỬA LỖI LOGIC: KIỂM TRA TRÙNG LẶP ***
        // Phải kiểm tra TẤT CẢ các mục tiêu được dùng trong hàm dominates()
        // =====================================================================
        bool identicalDist = std::abs(existingSolution.getTotalDistance() - newSolution.getTotalDistance()) < EPSILON;
        bool identicalVeh = existingSolution.getTotalVehicles() == newSolution.getTotalVehicles();
        bool identicalMaxTime = std::abs(existingSolution.getMaxTime() - newSolution.getMaxTime()) < EPSILON;
        bool identicalEnergy = std::abs(existingSolution.getTotalEnergy() - newSolution.getTotalEnergy()) < EPSILON;

        if (identicalDist && identicalVeh && identicalMaxTime && identicalEnergy)
        {
            return AddResult::IDENTICAL; // Bỏ qua nghiệm trùng lặp thực sự
        }
        // =====================================================================
        // (Kết thúc sửa lỗi)
        // =====================================================================


        // 2. Kiểm tra xem newSolution có bị thống trị không
        if (existingSolution.dominates(newSolution)) {
            isDominated = true;
            break;
        }

        // 3. Kiểm tra xem newSolution có thống trị nghiệm cũ không
        if (newSolution.dominates(existingSolution)) {
            dominatedIndices.push_back(i);
        }
    }

    if (isDominated) {
        return AddResult::DOMINATED;
    }

    // Xóa các nghiệm bị thống trị (nếu có)
    std::sort(dominatedIndices.rbegin(), dominatedIndices.rend());
    for (int index : dominatedIndices) {
        archive.erase(archive.begin() + index);
    }

    // Thêm nghiệm mới
    archive.push_back(newSolution);

    // Cắt tỉa nếu vượt quá kích thước
    if (archive.size() > maxSize) {
        prune();
    }

    if (!dominatedIndices.empty()) {
        return AddResult::DOMINATING;
    } else {
        return AddResult::NON_DOMINATED;
    }
}

// (Hàm getBestSolutionForObjective giữ nguyên)
Solution ParetoArchive::getBestSolutionForObjective(int objectiveIndex) const {
    if (archive.empty()) throw std::runtime_error("Archive is empty");
    auto compareFn = [&](const Solution& a, const Solution& b) {
        if (objectiveIndex == 0) return a.getTotalDistance() < b.getTotalDistance();
        if (objectiveIndex == 1) return a.getTotalVehicles() < b.getTotalVehicles();
        // SỬA: Thêm 2 mục tiêu còn lại (nếu cần)
        if (objectiveIndex == 2) return a.getMaxTime() < b.getMaxTime();
        if (objectiveIndex == 3) return a.getTotalEnergy() < b.getTotalEnergy();
        return a.getTotalDistance() < b.getTotalDistance(); // Mặc định
    };
    return *std::min_element(archive.begin(), archive.end(), compareFn);
}


// (Hàm prune giữ nguyên logic Crowding Distance)
// GHI CHÚ: Hàm prune() hiện đang hard-code 2 mục tiêu (Dist, Vehicles).
// Nếu bạn muốn dùng 4 mục tiêu, bạn cần cập nhật logic này.
void ParetoArchive::prune() {
    int n = archive.size();
    if (n <= maxSize) return;

    // *** CẢNH BÁO: Logic này chỉ đang dùng 2 MỤC TIÊU ***
    const int numObjectives = 2; // (Giả sử 2 mục tiêu: Dist, Vehicles)
    std::vector<double> crowdingDistances(n, 0.0);
    std::vector<int> indices(n);
    std::iota(indices.begin(), indices.end(), 0);

    for (int m = 0; m < numObjectives; ++m) {
        auto sortFn = [&](int a, int b) {
            if (m == 0) return archive[a].getTotalDistance() < archive[b].getTotalDistance();
            if (m == 1) return archive[a].getTotalVehicles() < archive[b].getTotalVehicles();
            // (Thêm mục tiêu 2, 3 nếu bạn muốn Crowding Distance 4D)
            return false;
        };
        std::sort(indices.begin(), indices.end(), sortFn);

        crowdingDistances[indices[0]] = std::numeric_limits<double>::infinity();
        crowdingDistances[indices[n - 1]] = std::numeric_limits<double>::infinity();

        double f_min, f_max;
        if (m == 0) {
            f_min = archive[indices[0]].getTotalDistance();
            f_max = archive[indices[n - 1]].getTotalDistance();
        } else {
            f_min = archive[indices[0]].getTotalVehicles();
            f_max = archive[indices[n - 1]].getTotalVehicles();
        }
        // (Thêm mục tiêu 2, 3...)

        double range = f_max - f_min;
        if (range < 1e-9) range = 1.0;

        for (int i = 1; i < n - 1; ++i) {
            double prevVal, nextVal;
            if (m == 0) {
                prevVal = archive[indices[i - 1]].getTotalDistance();
                nextVal = archive[indices[i + 1]].getTotalDistance();
            } else {
                prevVal = archive[indices[i - 1]].getTotalVehicles();
                nextVal = archive[indices[i + 1]].getTotalVehicles();
            }
            // (Thêm mục tiêu 2, 3...)
            crowdingDistances[indices[i]] += (nextVal - prevVal) / range;
        }
    }

    int worstIndex = std::min_element(crowdingDistances.begin(), crowdingDistances.end())
                   - crowdingDistances.begin();
    archive.erase(archive.begin() + worstIndex);
}