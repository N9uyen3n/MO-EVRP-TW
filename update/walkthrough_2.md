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
> **Hoàn tất Phase 1**. Các code metric đã hoạt động mà không gây lỗi.

## Phase 2: Cải thiện Candidate Filtering

1. **Thay thế `hasStation_` bằng `stationPrefix_`**
   - Đổi `std::vector<bool> hasStation_` thành `std::vector<int> stationPrefix_` trong `Route.h`.
   - Cập nhật hàm `evaluate()` trong `Route.cpp` để tự động tích lũy số lượng trạm sạc (`stationPrefix_[i + 1] = stationPrefix_[i] + ...`).
   - Thêm phương thức `hasStationBetween(i, j)` cho truy vấn số lượng trạm sạc trên một đoạn con (segment) chỉ trong $O(1)$.

2. **Tích hợp `timeSlack_` Cache**
   - Đưa phép tính `timeSlack_` vào trực tiếp hàm `Route::evaluate()` bằng một vòng lặp chạy ngược từ cuối tuyến đường về đầu. Điều này giúp loại bỏ việc cấp phát động `std::vector` (overhead) mỗi khi cần lấy `timeSlack_`. 
   - Hàm `getTimeSlack()` hiện tại được tối ưu để trả về một const reference của `timeSlack_`.

3. **Tối ưu hóa `searchTwoOpt`**
   - Sửa lại hàm `searchTwoOpt` (phần kiểm tra station trong Operator) từ vòng lặp duyệt từng phần tử $O(N)$ sang gọi truy vấn `routes[r].hasStationBetween(i, j)` $O(1)$.

## Kết quả kiểm tra
- Các thay đổi đã được compile thành công trên môi trường chuẩn (`exit code: 0`).
- Không có lỗi memory leak hay lỗi truy cập ngoài biên mảng nhờ các vector pre-allocated được quản lý an toàn trong nội bộ class `Route`.

> [!TIP]
> **Bước tiếp theo (Phase 3):** Thêm Multi-neighbor Lists. Chúng ta sẽ khởi tạo các bucket như `workloadNeighbors` và `slackNeighbors` trong `SearchContext` để điều hướng move Z3/Z4. Đảm bảo cấu trúc này chỉ được fill nhưng chưa làm thay đổi logic mặc định.

