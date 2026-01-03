# Kế hoạch Tối ưu hóa Hiệu năng Thuật toán ALNS

Mục tiêu: Tăng tốc độ giải thuật lên 2-3 lần bằng cách nhắm vào các điểm nóng (bottlenecks) đã được xác định.

## Bước 1: Loại bỏ `dynamic_pointer_cast` (Tăng tốc vi mô)

-   **Trạng thái**: ✅ **Đã hoàn thành**
-   **Vấn đề**: Sử dụng `dynamic_pointer_cast` nhiều lần trong các vòng lặp để kiểm tra loại `Node`, gây tốn chi phí runtime.
-   **Giải pháp**:
    1.  Thêm `enum class NodeType { CUSTOMER, STATION, DEPOT };` vào `Node.h`.
    2.  Thêm phương thức ảo `getType()` vào class `Node` cơ sở.
    3.  Ghi đè `getType()` trong các class `Customer`, `Station`, `Depot`.
    4.  Thay thế tất cả các lệnh gọi `dynamic_pointer_cast` bằng phép so sánh `node->getType() == NodeType::...`.
-   **Lợi ích**: Thay thế một phép kiểm tra kiểu động chậm bằng một phép so sánh số nguyên cực nhanh. An toàn và mang lại hiệu quả tích lũy lớn.

## Bước 2: Tối ưu hóa việc Sao chép đối tượng `Solution` (Tác động lớn)

-   **Vấn đề**: `Solution s_new = s_current;` trong vòng lặp chính của `ALNSSolver` có thể đang thực hiện "deep copy", gây tốn kém chi phí cấp phát và sao chép bộ nhớ.
-   **Giải pháp**: Thay đổi logic sao chép. Thay vì sao chép toàn bộ `Solution`, sẽ chỉ lưu trạng thái của các `Route` bị ảnh hưởng trước khi thay đổi. Nếu giải pháp mới không được chấp nhận, chỉ cần khôi phục lại các `Route` đã lưu.
-   **Lợi ích**: Giảm đáng kể gánh nặng bộ nhớ trong vòng lặp chính của thuật toán.

## Bước 3: Giảm cường độ `LocalSearch` (Tác động lớn nhất)

-   **Vấn đề**: `LocalSearch` (đặc biệt là `searchRelocate`) có độ phức tạp rất cao và là một trong những phần tốn thời gian nhất.
-   **Giải pháp (Heuristics)**:
    1.  **Giảm tần suất**: Giảm tần suất chạy `LocalSearch` (ví dụ: chạy ngẫu nhiên với một xác suất nhất định).
    2.  **Cắt tỉa không gian tìm kiếm**: Trong `searchRelocate`, chỉ xem xét di chuyển một khách hàng đến các vị trí gần `k` hàng xóm gần nhất (k-nearest neighbors) thay vì tất cả các vị trí.
-   **Lợi ích**: Tăng tốc độ đáng kể, có thể đánh đổi một chút chất lượng giải pháp nhưng thường sẽ đạt được sự cân bằng tốt.

## Bước 4: Giảm tần suất `RegretKRepair`

-   **Vấn đề**: Toán tử `RegretKRepair` có độ phức tạp rất cao (`O(d^2 * N * K)`).
-   **Giải pháp**: Giảm trọng số ban đầu (initial weight) của toán tử này trong `ALNSSolver` để giảm xác suất nó được chọn.
-   **Lợi ích**: Tránh được "kẻ hủy diệt hiệu năng" một cách đơn giản và hiệu quả.

---
*Kế hoạch sẽ được thực hiện tuần tự, bắt đầu từ Bước 1.*
