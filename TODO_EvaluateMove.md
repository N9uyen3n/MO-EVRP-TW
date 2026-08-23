# Báo cáo tiến độ Tối ưu hóa LocalSearch

## 1. Trạng thái hiện tại
*   **Thành công (applyStation):** Áp dụng cấp phát bộ đệm tĩnh (`thread_local std::unique_ptr<Route>`) cho `applyStation` đã giúp tăng tốc **17% (nhanh hơn ~6 giây)** trên bộ dữ liệu nhỏ `r105C15`.
*   **Thực tế trên bộ dữ liệu lớn (c101_21):** Mức độ tăng tốc chỉ đạt **0.11%**. Điều này chứng tỏ `applyStation` không phải là nút thắt cổ chai (bottleneck) chính ở các bộ dữ liệu lớn.
*   **Nút thắt thực sự (evaluateMove):** Đối với các bộ dữ liệu 100 khách hàng, hàm `evaluateMove` mới là nơi ngốn nhiều thời gian nhất do phải liên tục copy toàn bộ đối tượng `Route` (chứa 6 array và 2 shared_ptr) và gọi hàm `evaluate()` với độ phức tạp O(N).
*   **Vấn đề gặp phải (Crash):** Khi cố gắng áp dụng kỹ thuật cấp phát tĩnh (Static Object Pooling) cho `evaluateMove`, thuật toán chạy được một lúc nhưng bị Crash (văng lỗi) tại Segment số 2800. Nguyên nhân có thể do việc tham chiếu tĩnh trong lúc sửa đổi đối tượng `Route` gây dính líu bộ nhớ hoặc MSVC thread-safety overhead.

## 2. Các công việc cần làm tiếp theo (To-do List)

- [x] **[Khôi phục an toàn]:** Khôi phục lại hàm `evaluateMove` về trạng thái an toàn (sử dụng copy truyền thống).
    *   **Thực hiện:** Đã revert block `[OPT-13]` về `Route r1_copy = routes[...]`. Các file `src/alns/LocalSearch.cpp` và `test_localsearch/LocalSearch.cpp` đã được sửa và biên dịch thành công.
- [x] **[Tìm hiểu nguyên nhân Crash]:** Phân tích kỹ hơn tại sao con trỏ tĩnh `r1_ptr` và `r2_ptr` lại gây ra Access Violation.
    *   **Nguyên nhân:** Biến `static std::unique_ptr<Route>` được dùng nhưng **không có** từ khóa `thread_local`. Nếu thuật toán chạy đa luồng hoặc khi khởi chạy nhiều lần với các Instance khác nhau, con trỏ tĩnh này sẽ bị ghi đè chéo (Race Condition) hoặc giữ lại state của Instance cũ đã bị giải phóng, dẫn tới Access Violation. Cách copy truyền thống (`Route r1_copy = ...`) an toàn hơn rất nhiều và chi phí copy array không đáng kể so với việc chạy lại `evaluate()`.
- [ ] **[Hướng tối ưu mới - Đánh giá O(1) giả lập]:** Viết các vòng lặp kiểm tra tính khả thi O(1) (Delta Evaluation) cho `INTRA_RELOCATE` hay `INTRA_TWO_OPT` **trực tiếp ngay bên trong file `LocalSearch.cpp`**. 
    *   Bằng cách mô phỏng lại logic của Forward/Backward Pass (chỉ tính từ điểm cắt thay vì tính lại từ xe tải), ta có thể tăng tốc cực mạnh (>50%) cho bộ dữ liệu lớn mà không cần sửa `Route.h`.

## 3. Bước tiếp theo
Chờ phản hồi từ người dùng để xác nhận tiến hành viết Implementation Plan (Kế hoạch triển khai) cho Hướng tối ưu mới (Delta Evaluation O(1)).
