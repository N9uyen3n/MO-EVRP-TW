# Trong thư mục benchmark đang có 2 thư mục 1 là kết quả của thuật toán không có những thuật toán và tính năng liên quan tới việc cân bằng tải 
# và 1 thư mục có thêm những cái liên quan tới cân bằng tải là `no-EQ` và 2 là kết quả của thuật toán ở bản đầy đủ `full`, 
# Cấu trúc thư mục log `logs_test`

Tài liệu này mô tả cấu trúc phân cấp của thư mục `logs_test`, nơi lưu trữ toàn bộ dữ liệu log cho quá trình chạy thuật toán ALNS. Mặc định thuật toán chạy tự động cho từng instance và lưu trữ theo nhiều điểm gieo mầm (seed).

## Cấu trúc tổng quan

Cấu trúc lưu trữ được chia làm 3 cấp chính:

```text
bechnmark:
full or no-EQ/
├── c101C10/                     <-- Cấp 1: Tên Instance
│   ├── seed_1/                  <-- Cấp 2: Lần chạy thứ 1
│   │   ├── c101C10_seed_1_config.txt           <-- Cấp 3: Thông số cấu hình
│   │   ├── c101C10_seed_1_progress.csv         <-- Cấp 3: Tiến trình ALNS
│   │   ├── c101C10_seed_1_operators.csv        <-- Cấp 3: Hiệu năng toán tử
│   │   ├── c101C10_seed_1_front_objectives.csv <-- Cấp 3: Mục tiêu tập Pareto
│   │   ├── c101C10_seed_1_front_details.txt    <-- Cấp 3: Chi tiết tập Pareto
│   │   ├── c101C10_seed_1_summary.txt          <-- Cấp 3: Báo cáo tổng kết
│   │   └── c101C10_seed_1_evolution_log.txt    <-- Cấp 3: Nhật ký tiến hoá
│   ├── seed_2/                  <-- Cấp 2: Lần chạy thứ 2
│   ├── ...
│   └── seed_10/                 <-- Cấp 2: Lần chạy thứ 10
├── c101_21/                     <-- Cấp 1: Tên Instance (Tiếp tục)
├── r102C15/
└── ...
```

## Chi tiết Cấp 3: Ý nghĩa & Cấu trúc của 7 loại File Log

Mỗi thư mục lần chạy (ví dụ `seed_1/`) chứa **7 loại file log** đại diện cho một tiến trình ALNS hoàn chỉnh. Định danh tiền tố luôn là `[Tên_Instance]_seed_[Số_Seed]_`. 

### 1. Thẻ Cấu hình: `_config.txt`
*   **Định dạng:** Văn bản (.txt).
*   **Chức năng:** Lưu trữ "dấu vân tay" tham số đầu vào (Hyperparameters) của thuật toán.
*   **Chi tiết:** Chứa các giới hạn như `maxIterations`, `segmentIterations`, tốc độ làm lạnh `decayParameter`, các mốc tính điểm thưởng (scores) cho toán tử và cờ bật/tắt (Logging flags). Cực kỳ quan trọng để đảm bảo tính *reproducibility* (có thể tái lập lại thí nghiệm).

### 2. Log Tiến trình: `_progress.csv` (Quan trọng cho Vẽ Biểu đồ)
*   **Định dạng:** Dạng bảng (.csv), thiết kế Header theo định dạng CamelCase.
*   **Cấu trúc cột thực tế:**
    *   `Iteration`: Số thứ tự của vòng lặp hiện hành.
    *   `Timestamp_ms`: Cột mốc thời gian thực tế đã trôi qua (bằng mili-giây), thay vì đo time theo lý thuyết.
    *   `ArchiveSize`: Kích thước (Số lượng nghiệm) của tập Pareto ở thời điểm đo đạc.
    *   `BestDistance`: Tổng quãng đường (Z2) - Kỷ lục ngắn nhất hiện tại.
    *   `BestVehicles`: Biến số lượng xe (Z1) - Kỷ lục số xe ít nhất.
    *   `BestMaxTime`: Thời gian vòng rốn Makespan (Z4) tốt nhất.
    *   `BestWorkloadGini`: Chỉ số Gini phân bổ sức lao động / tải trọng (Z3) ưu việt nhất.
*   **Chức năng:** Là kho dữ liệu thô (raw data) duy nhất để phục vụ code Python (Phân tích Hội Tụ - Convergence Analysis) nhằm vẽ đồ thị mean/std theo tiến trình.

### 3. Log Quản lý Toán tử: `_operators.csv`
*   **Định dạng:** Dạng bảng (.csv), được ghi (dump) sau mỗi phân đoạn (Segment).
*   **Cấu trúc dữ liệu:** Liệt kê tên từng heuristic Operator (Destroy: Shaw, Random, Worst... / Repair: Greedy, Regret...), Điểm số (Score), Trọng số xác suất (Weight) và Tần suất sử dụng (Usage Count).
*   **Chức năng:** Dùng để minh hoạ thuật toán ALNS tự động học hỏi và điều chỉnh chiến lược như thế nào trong suốt các Segment. Giúp gỡ lỗi nếu có một toán tử chìm nghỉm vô dụng.

### 4. Dữ liệu Mặt Pareto (Global Objectives): `_front_objectives.csv`
*   **Định dạng:** Dạng bảng (.csv). Đóng vai trò là file quan trọng nhất đánh giá hiệu năng bài báo.
*   **Cấu trúc tối thiểu:** `Z1` (Số xe), `Z2` (Quãng đường), `Z3` (Workload/Gini), `Z4` (Makespan/MaxTime). 
*   **Chức năng:** Tổng hợp tất cả các nghiệm ưu việt nhất sau vòng lặp cuối cùng. Dùng để tính toán trực tiếp điểm Hypervolume (HV) 2D/4D và đối xứng mốc benchmark đa mục tiêu.

### 5. Cấu trúc Lộ trình (Routing Level): `_front_details.txt`
*   **Định dạng:** Văn bản (.txt) in format phân cấp cây.
*   **Chức năng:** Bản vẽ giải phẫu cấu trúc định tuyến chi tiết (Roadmap) của các nghiệm ưu việt.
*   **Chi tiết:** Trong mỗi nghiệm gồm Node xuất phát / Kết thúc, danh sách ID khách hàng trên từng xe, khối lượng nạp xả Cargo, điểm đỗ sạc (Partial Recharge Stations) trạm nào lượng sạc bao nhiêu, và cả timeline Arrival/Departure. Dùng để parse JSON vẽ hoạt hình hiển thị tuyến đường.

### 6. Hồ sơ Tổng kết: `_summary.txt`
*   **Định dạng:** Văn bản (.txt) ghi theo Key-Value.
*   **Cấu trúc Keys Thực Tế:**
    *   `TotalRunTime_ms`: Tổng thời gian chạy (mili-giây).
    *   `TotalRunTime_s`: Tổng thời gian chạy chuyển đổi (giây) - Phục vụ hiển thị xuất báo cáo.
    *   `TotalIterations`: Quy mô vòng lặp ALNS đã chạy.
    *   `FinalArchiveSize`: Quy mô mặt cắt Pareto thực thụ (Feasibility flag).
*   **Chức năng:** Được các Python Scripts tự động càn quét để bóc tách thông tin thô (Parse Regex) gộp thành bảng Summary Excel Cuối Cùng (BƯỚC 1).

### 7. Nhật ký Tiến hoá: `_evolution_log.txt`
*   **Định dạng:** Văn bản (.txt).
*   **Chức năng:** Ghi nhận lịch sử và tiến trình tiến hoá của thuật toán.
*   **Chi tiết:** Log lại kết quả (Accepted/Rejected/Dominating) ở các vòng lặp (iteration) cụ thể, kèm theo toán tử (Operators) đang được sử dụng và chi tiết cấu trúc lộ trình (Vehicles, Distance, Node Sequence...) của các nghiệm tương ứng tại thời điểm đó.
