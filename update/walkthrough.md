# Walkthrough: Phase 1 (Metrics & Tín hiệu Z3)

Dưới đây là tóm tắt những thay đổi đã được thực hiện trong **Phase 1** để thiết lập nền tảng đo lường cho các bước cải tiến Z3/Z4.

## Các thay đổi chính

1. **Thêm `gini` vào `LocalSearchWeights`**
   - Đã thêm `double gini = 0.0;` vào struct `LocalSearchWeights` trong [LocalSearch.h](file:///d:/Work/SLSCM-LaB/24092025/untitled/include/alns/LocalSearch.h). Việc này giúp chuẩn bị cho việc đánh giá move dựa trên workload variance/gini ở các operator trong tương lai, nhưng hiện tại không làm thay đổi luồng distance-centric.

2. **Nâng cấp `DiagCounters`**
   - Đã khai báo chi tiết các bộ đếm trong `DiagCounters` để đo lường toàn diện quá trình lọc move:
     - `generatedMoves`
     - `operatorRejectedMoves` (chưa dùng trong phase này, sẽ track sau ở từng op)
     - `fastPathRejectedMoves`
     - `copiedAndEvaluatedMoves`
     - `infeasibleAfterEvaluation`
     - `appliedMoves`
   - Đã thêm các bộ đếm thời gian (tính bằng micro giây) để tính tổng thời gian ở từng công đoạn: `totalEvalTime`, `totalCopyEvalTime`, `totalPatchTime`.

3. **Cập nhật `evaluateMove` và VND Loop**
   - Đã sử dụng kỹ thuật RAII (tạo struct `EvalScope`) bên trong hàm `LocalSearch::evaluateMove` tại [LocalSearch.cpp](file:///d:/Work/SLSCM-LaB/24092025/untitled/src/alns/LocalSearch.cpp) để tự động ghi nhận thời gian và tăng biến đếm tương ứng với mỗi fast path bị reject hoặc đi đến copy/eval.
   - Thêm bộ đếm thời gian cho hàm `patchSearchContext`.
   - Cập nhật hàm in thống kê `DiagCounters::print()` sang dạng bảng chi tiết (bao gồm cả thời gian tính bằng ms).

## Kết quả kiểm tra
- Toàn bộ source code đã được biên dịch thành công thông qua công cụ WSL CMake (lệnh `cmake --build cmake-build-debug-wsl -j 4`). Lỗi thiếu thư viện `#include <algorithm>` đã được sửa.
- Việc chèn logging và counters không phá vỡ logic cũ do chúng tôi đã dùng một biến trạng thái an toàn `currentOperator_` để track operator nào đang được tính.

> [!TIP]
> **Bước tiếp theo (Phase 2):** Bạn có thể tiến hành test benchmark lại trên local của mình với lệnh thường dùng để xem bảng diagnostic chi tiết chạy như thế nào. Nếu mọi thứ hiển thị chuẩn, tôi có thể chuyển sang **Phase 2**, cụ thể là bổ sung các logic lọc nhanh và các cache như `stationPrefix` / `timeSlack_` (nếu phù hợp).
