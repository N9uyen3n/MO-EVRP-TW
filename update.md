Dưới đây là nội dung chi tiết cho phần **"Định nghĩa bài toán" (Problem Formulation)** được viết lại theo văn phong khoa học, chuẩn mực và chính xác nhất dựa trên các ý tưởng về **Công bằng Tài xế (Driver Equity)** và **Chiến lược Đơn giản hóa Thích ứng (Adaptive Scalarization)** mà chúng ta đã thống nhất.

Bạn có thể copy trực tiếp phần này vào mục **2. Mathematical Model** của bài báo.

---

## 2. Định nghĩa Bài toán (Mathematical Formulation)

Chúng ta xem xét bài toán Tìm đường đi cho xe điện với cửa sổ thời gian và ràng buộc công bằng tài xế như một bài toán tối ưu hóa nhiều mục tiêu (Bi-objective Optimization Problem). Mục tiêu của bài toán là tìm ra tập hợp các lộ trình vận hành tối ưu, trong đó cân bằng giữa việc giảm chi phí vận hành và đảm bảo sự công bằng cho các tài xế.

### 2.1. Tập hợp và Tham số (Sets and Parameters)

Cho $G=(V,E)$ là một đồ thị có hướng, trong đó:

- $C$: Tập hợp các khách hàng (customers).  
- $S$: Tập hợp các trạm sạc (stations).  
- $\{0\}$: Điểm kho (depot).  
- $K$: Tập hợp các xe (vehicles) đồng nhất.  
- $K_u \subseteq K$: Tập hợp các xe được sử dụng trong giải pháp.  
- $N = C \cup S \cup \{0\}$: Tập hợp tất cả các nút.

Các tham số của bài toán bao gồm:

- $d_{ij}$: Khoảng cách (chi phí di chuyển) giữa nút $i$ và nút $j$.  
- $t_{ij}$: Thời gian di chuyển giữa nút $i$ và nút $j$.  
- $e_i, l_i$: Cửa sổ thời gian (time window) của nút $i$ ($e_i$: sớm nhất, $l_i$: muộn nhất).  
- $q_i$: Nhu cầu (demand) của khách hàng $i$.  
- $Q$: Dung tích tối đa của xe.  
- $B$: Dung lượng pin tối đa của xe.  
- $r$: Tỷ lệ tiêu thụ năng lượng trên một đơn vị khoảng cách.  

Với mỗi xe $k \in K_u$, ký hiệu $T_k$ là tổng thời gian hoàn thành lộ trình (bao gồm thời gian di chuyển, phục vụ và sạc).

---

### 2.2. Các Biến Quyết Định (Decision Variables)

Các biến quyết định của bài toán được chia thành hai nhóm:

#### 1. Biến cấu trúc lộ trình (Routing Variables)

- $x_{ijk} \in \{0, 1\}$: Bằng 1 nếu xe $k$ đi trực tiếp từ nút $i$ đến nút $j$, và bằng 0 nếu ngược lại.  
- $y_{ik} \in \{0, 1\}$: Bằng 1 nếu nút $i$ được phục vụ bởi xe $k$.  
- $N_{used} \in \mathbb{Z}^+$: Số lượng xe được sử dụng trong giải pháp.  

#### 2. Biến điều chỉnh năng lượng (Energy Control Variables)

- $c_{ik} \ge 0$: Lượng năng lượng được sạc cho xe $k$ tại trạm sạc $i$. Trong mô hình này, lượng sạc không cố định mà được tối ưu hóa như một biến quyết định phụ thuộc vào cấu trúc lộ trình.

---

### 2.3. Các Hàm Mục Tiêu (Objective Functions)

Mục tiêu của bài toán là tối ưu hóa hai khía cạnh:

- **Hiệu quả Chi phí Vận hành (Operational Cost)**  
- **Công bằng Tài Xế (Driver Equity)**  

---

#### 2.3.1. Mục tiêu 1: Hiệu quả Chi phí Vận hành (Cost Efficiency)

Mục tiêu chính là giảm thiểu tổng chi phí vận hành bao gồm chi phí di chuyển và (ngầm định) chi phí cố định cho mỗi xe. Ngoài ra, giảm số lượng xe được coi là ưu tiên có tính thứ bậc (Hierarchical Objective).

$$
\min Z_1(s) = \sum_{k \in K_u} \sum_{i,j \in N} d_{ij} x_{ijk}
$$

Để đơn giản hóa ký hiệu khi so sánh nghiệm, chi phí cố định cho mỗi xe được xem là đã tích hợp vào thành phần quãng đường (hoặc bằng 0 trong mô hình chuẩn hóa).

---

#### 2.3.2. Mục tiêu 2: Công bằng Tài Xế (Driver Equity)

Để đo lường sự công bằng giữa các tài xế, chúng ta sử dụng **Hệ số Gini (Gini Coefficient)** thay vì phương sai. Hệ số này phản ánh mức độ chênh lệch về tổng thời gian làm việc giữa các xe.

Gọi $T_k$ là tổng thời gian hoàn thành lộ trình của xe $k$, hệ số Gini $G(s)$ được định nghĩa như sau:

$$
G(s) = \frac{\sum_{k=1}^{|K_u|} \sum_{j=1}^{|K_u|} |T_k - T_j|}{2 |K_u| \sum_{j=1}^{|K_u|} T_j}
$$

Trong đó:

- $|K_u|$: Số lượng xe được sử dụng.  
- $\sum_{j} T_j$: Tổng thời gian làm việc của tất cả các xe.  
- $G(s) \in [0,1]$:  
  - $G(s)=0$: Công bằng hoàn toàn (tất cả xe có thời gian làm việc bằng nhau).  
  - $G(s) \to 1$: Mức bất công bằng cao.

**Mục tiêu:**  

$$
\min G(s)
$$

---

### 2.4. Chiến lược Đơn giản hóa Thích ứng (Adaptive Scalarization Strategy)

Vì thuật toán tìm kiếm (ALNS kết hợp Simulated Annealing) yêu cầu một giá trị vô hướng (scalar value) để so sánh chất lượng giữa các giải pháp và xác định xác suất chấp nhận, chúng ta chuyển bài toán đa mục tiêu thành bài toán đơn mục tiêu bằng kỹ thuật **Đơn giản hóa Thích ứng (Adaptive Scalarization)**.

Hàm mục tiêu tổng hợp được định nghĩa như sau:

$$
Z(s, t) =
w_V \cdot \hat{Z}_{veh}(s)
+ w_D \cdot \hat{Z}_{dist}(s)
+ w_F(t) \cdot \hat{Z}_{gini}(s)
$$

Trong đó:

---

#### 1. Chuẩn hóa (Normalization)

Để đảm bảo các thành phần có cùng thang đo, các mục tiêu được chuẩn hóa về khoảng $[0,1]$:

- $\hat{Z}_{veh}(s) = \dfrac{N_{used}(s)}{N_{ref}}$  
- $\hat{Z}_{dist}(s) = \dfrac{Dist(s)}{D_{ref}}$  
- $\hat{Z}_{gini}(s) = \dfrac{G(s)}{G_{ref}}$  

Với $N_{ref}, D_{ref}, G_{ref}$ là các giá trị tham chiếu (ví dụ: từ nghiệm khởi tạo hoặc Best Known Solution).

---

#### 2. Các trọng số (Weights)

- $w_V$: Trọng số cho số xe. Đặt rất lớn (ví dụ $1000.0$) để đảm bảo ưu tiên giảm số lượng xe.  
- $w_D$: Trọng số cho quãng đường (thường đặt $1.0$).  
- $w_F(t)$: **Trọng số thích ứng cho Công bằng**, thay đổi theo thời gian tìm kiếm $t$.  

---

#### 3. Cơ chế thích ứng cho trọng số công bằng

Trọng số $w_F(t)$ được điều chỉnh theo tiến trình tìm kiếm:

$$
w_F(t)=
\begin{cases}
\lambda_{start}, & 0 \le t < \tau T_{max} \\
\lambda_{end}, & t \ge T_{max} \\
\lambda_{start} + 
\frac{t - \tau T_{max}}{(1-\tau)T_{max}}
(\lambda_{end} - \lambda_{start}), & \text{otherwise}
\end{cases}
$$

Trong đó:

- $\lambda_{start}$: Trọng số ban đầu (thấp, ví dụ $0.1$) – giai đoạn tập trung tối ưu chi phí.  
- $\lambda_{end}$: Trọng số cuối (cao, ví dụ $2.0$–$5.0$) – giai đoạn tinh chỉnh công bằng.  
- $\tau \in (0,1)$: Ngưỡng chuyển pha.  
- $T_{max}$: Tổng số vòng lặp tối đa.  

---

### Kết luận

Hàm mục tiêu $Z(s,t)$ cho phép thuật toán:

- Giai đoạn đầu: tập trung vào giảm số xe và quãng đường.  
- Giai đoạn sau: giữ chi phí ở mức tốt nhưng gia tăng mạnh áp lực tối ưu công bằng (giảm hệ số Gini).  

Cách tiếp cận này giúp tìm được các nghiệm vừa hiệu quả vận hành vừa đảm bảo tính công bằng cho tài xế trong hệ thống phân phối sử dụng xe điện.


Đây là một kế hoạch thiết kế **thực nghiệm quy mô nhỏ (Small-Scale Experimentation)** chuẩn mực, giúp bạn tạo ra một "Dòng cơ sở" (Baseline) vững chắc trước khi cải tiến sâu code trong 2 tháng tới.

Việc chạy thực nghiệm này rất quan trọng để:

1. **Chốt tham số Alpha/Beta** cho công thức Adaptive Weights.
2. **Đo lường hiệu năng** của việc Gộp Toán tử (Operator Merging) so với Code cũ.
3. **Kiểm tra tính đúng sai** của các hàm mới (`calculateGini`, `ObjectiveEvaluator`).

---

## 1. Chiến lược chọn dữ liệu (Data Selection Strategy)

Để bộ dữ liệu 8–10 instance có tính đại diện (Representative) cho bộ Solomon, bạn nên chọn theo tiêu chí **Phân loại theo loại dữ liệu (Data Type)**.
có thể lấy ở "data/test1", các thông tin liên quan tới BKS có thể lấy trong  BKS_100.txt và BKS.txt
---
Nhiệm vụ 1: So sánh với Đường cơ sở (Baselines)
Bạn cần chạy ít nhất 3 phiên bản thuật toán trên cùng bộ dữ liệu Solomon (rc108, c101, r201...) để đối chứng:

Baseline A (Cost-Only): Thuật toán hiện tại của bạn, nhưng tắt hoàn toàn mục tiêu Fairness (
w 
F
​
 =0
).
Mục đích: Chứng minh thuật toán mạnh về tối ưu hóa chi phí (Gap Distance ~5-10%).
Baseline B (Static Weighted): Dùng công thức tổng hợp nhưng giữ trọng số cố định (
w 
D
​
 =1,w 
F
​
 =1
) trong suốt quá trình.
Mục đích: Chứng minh rằng trọng số cố định kém hiệu quả hơn cơ chế thích ứng của bạn.
Proposed Method (Adaptive Fairness): Sử dụng công thức 
Z(t)
 với 
λ(t)
 thay đổi theo giai đoạn.
Mục đích: Chứng minh ưu điểm vượt trội về sự cân bằng.
Nhiệm vụ 2: Phân tích đánh đổi (Trade-off Analysis)
Đây là phần quan trọng nhất để minh họa Pareto Front.

Thiết kế: Vẽ biểu đồ Scatter 2D.
Trục X: Total Distance (Hoặc Normalized Cost).
Trục Y: Gini Coefficient (0 là tốt nhất, 1 là tệ nhất).
Biểu đồ: Vẽ các điểm của 3 thuật toán A, B, C lên cùng một biểu đồ.
Biện luận:
Baseline A (Cost-Only): Sẽ nằm cực trái (Distance tốt nhất) nhưng ở trên cao (Gini tệ nhất - bất công).
Baseline B (Static): Sẽ nằm ở giữa (Cải thiện chút Gini nhưng giảm Distance).
Proposed Method (Adaptive): Nằm ở góc dưới bên trái (Distance tốt, Gini thấp -> Công bằng). Đây chính là vùng "Win-Win" bạn cần nhấn mạnh.
Nhiệm vụ 3: Phân tích hội tụ (Convergence Analysis)
Chứng minh cơ chế thích ứng hoạt động đúng như giả thuyết.

Thiết kế: Vẽ biểu đồ đường thẳng (Line Chart) theo thời gian (Iterations).
Đường 1: Normalized Distance.
Đường 2: Normalized Gini.
Biện luận:
Giai đoạn đầu (0-30%): Đường Distance giảm mạnh (tìm lộ trình tốt), Đường Gini giảm chậm hoặc phẳng.
Giai đoạn sau (70-100%): Đường Distance ổn định, Đường Gini giảm mạnh (tinh chỉnh lại phân bổ tải).
Kết luận: Biểu đồ này là "bằng chứng thép" (Proof) cho hiệu quả của cơ chế Adaptive.
