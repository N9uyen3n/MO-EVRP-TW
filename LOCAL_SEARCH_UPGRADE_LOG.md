# Log Nâng Cấp Module Local Search

Tài liệu này ghi lại các thay đổi, sửa lỗi và tối ưu hóa đã được áp dụng cho module `LocalSearch` để cải thiện hiệu năng và tính đúng đắn.

---

## 1. Tổng Quan Các Nâng Cấp

Module `LocalSearch` đã được tái cấu trúc và hoàn thiện đáng kể, tập trung vào 3 mảng chính:

1.  **Sửa Lỗi Nghiêm Trọng (Bug Fixes):** Hoàn thiện logic xử lý các loại nước đi (moves) còn thiếu.
2.  **Hoàn Thiện Toán Tử (Operator Completion):** Kích hoạt các toán tử đã có và triển khai các toán tử mới, đặc biệt là `runVehicleReduction` để tối ưu mục tiêu số một (số lượng xe).
3.  **Tối Ưu Hóa Hiệu Năng (Performance Tuning):** Áp dụng các kỹ thuật đã được kiểm chứng để tăng tốc độ thực thi của Local Search.

---

## 2. Chi Tiết Các Thay Đổi

### 2.1. Sửa Lỗi & Hoàn Thiện Logic

-   **Bổ sung `evaluateMove()`:**
    -   Hàm `evaluateMove` đã được bổ sung logic để xử lý các loại nước đi còn thiếu, bao gồm:
        -   `MoveType::INTRA_SWAP` (đổi chỗ 2 khách hàng trong cùng 1 tuyến)
        -   `MoveType::INTER_SWAP` (đổi chỗ 2 khách hàng giữa 2 tuyến khác nhau)
        -   `MoveType::STATION_REMOVAL` (xóa một trạm sạc khỏi tuyến)
    -   *Lý do:* Trước đây, các toán tử `searchSwap` và `searchStationRemoval` không thể hoạt động đúng vì `evaluateMove` không hiểu các nước đi này.

-   **Bổ sung `applyMove()`:**
    -   Tương tự, hàm `applyMove` đã được hoàn thiện để có thể **thực thi** các nước đi `INTRA_SWAP`, `INTER_SWAP`, và `STATION_REMOVAL` sau khi chúng được lựa chọn.
    -   *Lý do:* Đảm bảo tính nhất quán giữa việc đánh giá và việc áp dụng nước đi.

### 2.2. Hoàn Thiện & Bổ Sung Toán Tử

-   **Kích hoạt `searchSwap`:**
    -   Toán tử `searchSwap` (đã được viết trước đó) nay đã được gọi chính thức trong hàm `runDistanceOptimization`.
    -   *Lý do:* Bổ sung thêm một "vũ khí" mạnh mẽ vào bộ công cụ tối ưu hóa khoảng cách, giúp thuật toán thoát khỏi các điểm tối ưu cục bộ tốt hơn.

-   **Triển khai `runVehicleReduction`:**
    -   Đây là một nâng cấp **quan trọng nhất**, trực tiếp tác động đến mục tiêu số một của bài toán.
    -   **Chiến lược:**
        1.  Ưu tiên các tuyến đường có ít khách hàng (`<= 5`).
        2.  Tìm một tuyến đường khác ở gần (dựa trên `areRoutesClose`).
        3.  Thử gộp toàn bộ khách hàng từ tuyến nhỏ vào tuyến lớn hơn.
        4.  Nếu tuyến đường sau khi gộp vẫn hợp lệ (`isFeasible`), xóa tuyến đường nhỏ ban đầu.
        5.  Sử dụng chiến lược "First Improvement" - áp dụng ngay lần gộp thành công đầu tiên.
    -   *Lý do:* Giảm số lượng xe là ưu tiên cao nhất, và toán tử này được thiết kế đặc biệt cho mục tiêu đó.

### 2.3. Tối Ưu Hóa Hiệu Năng

-   **Áp dụng "First Improvement":**
    -   Các toán tử có không gian tìm kiếm lớn là `searchRelocate` và `searchSwap` đã được chuyển từ chiến lược "Best Improvement" (tìm nước đi tốt nhất) sang "First Improvement" (áp dụng ngay nước đi cải thiện đầu tiên tìm thấy).
    -   *Lý do:* Giảm đáng kể thời gian tìm kiếm. Thay vì phải duyệt toàn bộ không gian, thuật toán có thể nhanh chóng thực hiện một nước đi tốt và tiếp tục vòng lặp, giúp tăng tốc độ hội tụ tổng thể. `searchTwoOpt` vẫn giữ "Best Improvement" vì không gian tìm kiếm của nó nhỏ hơn.

-   **Áp dụng "Static Move Descriptor" (SMD) - Tối ưu bộ nhớ:**
    -   Trong các hàm `searchRelocate`, `searchSwap`, và `searchTwoOpt`, đối tượng `MoveDescriptor` đã được di chuyển ra khỏi các vòng lặp `for`.
    -   **Cơ chế:** Đối tượng `MoveDescriptor` giờ đây được khai báo một lần duy nhất ở đầu mỗi hàm và được tái sử dụng trong các vòng lặp thông qua lệnh `move.reset()`.
    -   *Lý do:* Loại bỏ hàng triệu lệnh cấp phát và hủy đối tượng không cần thiết trên stack, giúp giảm overhead và tăng tốc độ thực thi của các vòng lặp nóng (hot loops).

-   **Tăng cường "Tiered Filtering" - Kiểm tra Tải trọng:**
    -   **Mục tiêu:** Thêm một tầng lọc (Tier 2) để loại bỏ các nước đi không khả thi về tải trọng trước khi gọi `evaluateMove` tốn kém.
    -   **Triển khai:**
        1.  **Thêm `Route::getTotalDemand()`:** Bổ sung phương thức mới để tính chính xác tổng nhu cầu của một tuyến.
        2.  **Thêm `Route::getVehicle()`:** Bổ sung phương thức để truy cập vào xe của tuyến, từ đó lấy được sức chứa tối đa.
        3.  **Tích hợp vào Local Search:** `searchRelocate` và `searchSwap` giờ đây sẽ sử dụng các hàm trên để thực hiện kiểm tra tải trọng. Nếu một nước đi (relocate hoặc swap) làm cho bất kỳ tuyến nào bị quá tải, nó sẽ bị loại bỏ ngay lập tức.
    -   *Lý do:* Ngăn chặn việc đánh giá các nước đi chắc chắn không hợp lệ, giúp tiết kiệm tài nguyên tính toán và tăng tốc độ của Local Search.

---

## 3. Tác Động & Kết Quả

Những nâng cấp trên giúp cho module `LocalSearch` trở nên:
-   **Mạnh mẽ hơn:** Có khả năng tối ưu trên nhiều phương diện hơn (swap, giảm xe).
-   **Chính xác hơn:** Sửa các lỗi logic khiến các toán tử không hoạt động.
-   **Nhanh hơn đáng kể:** Nhờ vào chiến lược "First Improvement", tối ưu hóa bộ nhớ SMD, và các tầng lọc kiểm tra thông minh.