// include/alns/LocalSearch.h (ĐÃ NÂNG CẤP TOÀN DIỆN)
#pragma once
#include "core/Solution.h"
#include "core/Instance.h"
#include <memory>  // <-- THÊM VÀO
#include <vector>  // <-- THÊM VÀO

// --- 1. Bộ mô tả nước đi (Static Move Descriptor) ---

enum class MoveType {
    RELOCATE,
    SWAP,
    TWO_OPT,        // <-- ĐÃ THÊM
    STATION_INSERT, // <-- ĐÃ THÊM
    STATION_REMOVE  // <-- ĐÃ THÊM
};

// (Giữ nguyên MoveEvaluation)
struct MoveEvaluation {
    bool isFeasible = false;
    double objectiveDelta = 0.0;
};

// (Giữ nguyên MoveDescriptor)
struct MoveDescriptor {
    MoveType type;
    MoveEvaluation eval;

    int customer1, customer2;
    int routeIdx1, routeIdx2;
    int pos1, pos2;

    // (Các trường này đã có sẵn, giờ chúng ta sẽ dùng)
    int stationId = -1;
};


// --- 2. Lớp Local Search chính (NÂNG CẤP) ---

class LocalSearch {
public:
    /**
     * @brief (SỬA) Thêm constructor để nhận Instance.
     * Cần thiết để truy cập danh sách trạm sạc.
     */
    LocalSearch(std::shared_ptr<Instance> inst);

    /**
     * @brief Áp dụng Local Search (ví dụ: Best Improvement) trên nghiệm.
     */
    void run(Solution& solution);

private:
    std::shared_ptr<Instance> instance; // <-- THÊM: Để truy cập dữ liệu instance
    std::vector<int> stationIds;        // <-- THÊM: Cache danh sách trạm sạc

    // --- Các lân cận tiêu chuẩn ---
    bool findBestRelocateMove(const Solution& solution, MoveDescriptor& out_bestMove);
    bool findBestSwapMove(const Solution& solution, MoveDescriptor& out_bestMove);

    // --- CÁC LÂN CẬN MỚI "THÔNG MINH" HƠN ---

    /**
     * @brief Tìm nước đi 2-Opt tốt nhất (cải thiện quãng đường nội tuyến).
     */
    bool findBestTwoOptMove(const Solution& solution, MoveDescriptor& out_bestMove);

    /**
     * @brief Tìm vị trí chèn trạm sạc tốt nhất (cải thiện năng lượng/chi phí).
     */
    bool findBestStationInsertionMove(const Solution& solution, MoveDescriptor& out_bestMove);

    /**
     * @brief Tìm trạm sạc không cần thiết/tệ nhất để xóa.
     */
    bool findBestStationRemovalMove(const Solution& solution, MoveDescriptor& out_bestMove);


    // --- Các hàm lõi ---

    /**
     * @brief Đánh giá NHANH một nước đi (SMD).
     */
    MoveEvaluation evaluateMove(const Solution& solution, const MoveDescriptor& move);

    /**
     * @brief Áp dụng nước đi đã chọn vào nghiệm.
     */
    void applyMove(Solution& solution, const MoveDescriptor& move);
};