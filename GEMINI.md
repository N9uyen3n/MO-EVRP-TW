1. 🎯 Tổng Quan Dự Án
- Bài toán: Electric Vehicle Routing Problem with Time Windows (EVRPTW).
- Phương pháp giải: Hybrid ALNS (Adaptive Large Neighborhood Search) kết hợp Pareto Optimization.
- Mục tiêu: Tối ưu hóa Đa mục tiêu (Multi-objective).
- Tech Stack: C++ (Standard 17/20), CMake, CLion/VS.

2. 🏗️ Kiến Trúc Hệ Thống (Project Structure)

Dưới đây là cấu trúc các tệp và thư mục mã nguồn quan trọng trong dự án. Cấu trúc này tuân thủ nguyên tắc tách biệt giao diện (`.h` trong `include/`) và triển khai (`.cpp` trong `src/`).

```text
.
├── CMakeLists.txt
├── GEMINI.md
├── main.cpp
├── OPTIMIZATION_PLAN.md
├── include
│   ├── alns
│   │   ├── ALNSSolver.h
│   │   ├── IOperator.h
│   │   ├── LocalSearch.h
│   │   ├── ParetoArchive.h
│   │   ├── SolutionPool.h
│   │   └── operators
│   │       ├── destroy
│   │       │   ├── InefficientRouteRemoval.h
│   │       │   ├── RouteMergingDestroy.h
│   │       │   ├── ShawDestroy.h
│   │       │   └── UnifiedCostDestroy.h
│   │       └── repair
│   │           ├── AdaptiveInsertion.h
│   │           ├── GreedyEnergyInsertion.h
│   │           ├── ParetoFocusRepair.h
│   │           ├── RegretKRepair.h
│   │           └── SmartStationRepair.h
│   ├── app
│   │   └── AppController.h
│   ├── core
│   │   ├── Customer.h
│   │   ├── Depot.h
│   │   ├── Instance.h
│   │   ├── Node.h
│   │   ├── Route.h
│   │   ├── Solution.h
│   │   ├── Solver.h
│   │   ├── Station.h
│   │   └── Vehicle.h
│   ├── io
│   │   └── Parser.h
│   ├── logger
│   │   ├── ComprehensiveLogger.h
│   │   ├── ILogger.h
│   │   └── NullLogger.h
│   └── utils
│       ├── CmdLineParser.h
│       └── Utils.h
└── src
    ├── alns
    │   ├── ALNSSolver.cpp
    │   ├── LocalSearch.cpp
    │   ├── ParetoArchive.cpp
    │   ├── SolutionPool.cpp
    │   └── operators
    │       ├── destroy
    │       │   ├── InefficientRouteRemoval.cpp
    │       │   ├── RouteMergingDestroy.cpp
    │       │   ├── ShawDestroy.cpp
    │       │   └── UnifiedCostDestroy.cpp
    │       └── repair
    │           ├── AdaptiveInsertion.cpp
    │           ├── GreedyEnergyInsertion.cpp
    │           ├── ParetoFocusRepair.cpp
    │           ├── RegretKRepair.cpp
    │           └── SmartStationRepair.cpp
    ├── app
    │   └── AppController.cpp
    ├── core
    │   ├── Customer.cpp
    │   ├── Depot.cpp
    │   ├── Instance.cpp
    │   ├── Node.cpp
    │   ├── Route.cpp
    │   ├── Solution.cpp
    │   ├── Solver.cpp
    │   ├── Station.cpp
    │   └── Vehicle.cpp
    ├── io
    │   └── Parser.cpp
    ├── logger
    │   └── ComprehensiveLogger.cpp
    ├── main.cpp
    ├── test
    │   └── TestALNS.cpp
    ├── tuning
    │   └── TuningApp.cpp
    └── utils
        └── Utils.cpp
```

3. ⚙️ Logic Cốt Lõi (CRITICAL RULES)

A. Ràng buộc Khả thi (Feasibility) - Route::evaluate()
Đây là "trái tim" của việc kiểm tra đúng sai. KHÔNG ĐƯỢC PHÉP làm sai lệch logic này.
1. Năng lượng (Battery): Sử dụng thuật toán Backward Pass (duyệt ngược) để tính năng lượng tối thiểu cần thiết (min_req). Xe phải sạc đủ để đi đến trạm tiếp
   theo.
2. Thời gian (Time Window): Kiểm tra ReadyTime, DueDate và ServiceTime.
3. Tải trọng (Capacity): Không vượt quá tải của xe.
4. Lưu ý API: Route::evaluate() là hàm void. Sau khi gọi, phải dùng các getter như isFeasible(), getTotalDistance() để lấy kết quả được cập nhật bên trong đối
   tượng Route.

B. Định nghĩa Mục tiêu (Objectives)
Khi tính toán Cost hoặc so sánh, phải tuân thủ định nghĩa:
1. Total Vehicles: Số lượng xe sử dụng (Càng ít càng tốt).
2. Total Distance: Tổng quãng đường di chuyển.
3. **Workload Balance (Cân Bằng Tải):** Tối thiểu hóa phương sai của tổng thời gian hoạt động trên các tuyến (`variance of route durations`).
    - **Ý nghĩa:** Mục tiêu này hướng đến việc phân chia công việc một cách công bằng cho các tài xế, tránh tình trạng có người làm việc quá nhiều trong khi người khác lại quá ít. Một phương sai nhỏ cho thấy các tuyến có thời gian làm việc tương đồng nhau.
    - **Công thức:** `Variance = (1/|V_u|) * Σ (T_k - μ_T)²` với `T_k` là thời gian tuyến `k`, `μ_T` là thời gian trung bình, và `V_u` là tập xe được sử dụng.
4. Max Time / Makespan: Thời gian hoàn thành trễ nhất.

C. Cơ chế So sánh Pareto (Solution::dominates)
Hàm so sánh trội phải phân cấp (Hierarchical):
- Ưu tiên 1 (Tuyệt đối): Số lượng xe (totalVehicles).
    - Nếu A ít xe hơn B -> A thắng ngay lập tức.
- Ưu tiên 2: Mới xét đến Pareto của các mục tiêu còn lại (Distance, Energy, Time).
> Lý do: Để tránh việc thuật toán giữ lại các giải pháp "mỗi khách 1 xe" chỉ vì nó tối ưu về năng lượng.

D. Chiến lược ALNS & Local Search
- ALNS Main Loop: Dùng Weighted Sum (Tổng trọng số) để định hướng tìm kiếm (Simulated Annealing) nhằm đảm bảo sự hội tụ. Dùng Pareto Archive để lưu trữ kết quả đầu ra.
- **Vehicle Reduction Phase (Chiến lược giảm xe):**
    - Được kích hoạt định kỳ hoặc khi thuật toán không cải thiện (stagnation).
    - **Smart Multi-Route Merge:** Gộp 2 tuyến có trọng tâm gần nhau và tổng kích thước < 35. Sử dụng cơ chế Station-Assisted Reconstruction (tự động chèn trạm sạc) để kết nối các tuyến dài mà không vi phạm năng lượng.
    - **Smallest Route Elimination:** Cố gắng "xóa sổ" tuyến nhỏ nhất (< 15 khách) bằng cách phân tán khách hàng sang các tuyến khác. Nếu chèn thất bại, thuật toán thử chèn thêm trạm sạc để cứu vãn (Station-Assisted Insertion).
- **Local Search Optimizations:**
    - **Static Move Descriptor (SMD):** Tái sử dụng đối tượng `MoveDescriptor` để tránh cấp phát bộ nhớ liên tục trong vòng lặp (Zero-Allocation Loop).
    - **Pruning & Fast Delta Check:** Sử dụng `evaluateRelocateDelta` và `checkInsertionCost` (Tier 2 Fast Check) để loại bỏ >80% các move không khả thi hoặc làm tăng chi phí trước khi thực hiện `evaluate()` (Deep Copy).
    - **Tiered Filtering:** Kiểm tra theo tầng: Capacity/Time Check (Basic) -> Tier 2 Fast Check -> Tier 3 Full Evaluation.
    - **First Improvement:** Áp dụng ngay move cải thiện đầu tiên tìm thấy đối với các toán tử đơn giản (Relocate, Swap) để tăng tốc độ hội tụ.
    - **Granular Neighborhood:** Chỉ tìm kiếm các node hàng xóm gần nhất (KNN) và các tuyến lân cận (Centroid Distance) để thu hẹp không gian tìm kiếm.

4. 📝 Coding Conventions
- Pointer: Sử dụng std::shared_ptr cho Node và Instance.
- Loop: Chú ý warning ép kiểu size_t sang int (dùng static_cast<int>).
- Memory: Tránh copy deep Solution trong vòng lặp Local Search nếu không cần thiết (chỉ copy Route).

5. 🚫 Bài Học Xương Máu (Known Issues & Fixes)
   Những lỗi đã từng gặp, tuyệt đối không lặp lại:

1. Lỗi "Energy = 0":
    - Triệu chứng: Kết quả ra Energy = 0.0000 dù xe có chạy.
    - Nguyên nhân: Do tính tổng lượng sạc (chargeAmount) thay vì lượng tiêu thụ.
    - Fix: evalResult.totalEnergy += consumption; trong vòng lặp Forward của Route.cpp.

2. Lỗi "Mỗi khách 1 xe" (Stagnation):
    - Triệu chứng: 25 khách dùng 25 xe, thuật toán không chịu gộp tuyến.
    - Nguyên nhân: Hàm dominates coi trọng Energy (bằng 0) ngang hàng với số xe.
    - Fix: Đặt điều kiện totalVehicles lên đầu hàm dominates.

3. Lỗi Feasibility ảo trong Local Search:
    - Triệu chứng: Local Search hoặc Repair Operator báo cải thiện, nhưng khi check lại thì Infeasible (âm pin).
    - Nguyên nhân: Logic tính toán delta (thay đổi chi phí) không bao quát hết các ràng buộc phức tạp, đặc biệt là logic sạc pin khi chèn thêm trạm sạc.
    - Fix: Luôn gọi evaluate() trên một bản sao của Route (Route copy = originalRoute;) khi cần kiểm tra một nước đi (move) tiềm năng. Không được "giả định"
      tính khả thi.

4. Lỗi "Rơi" khách hàng trong Repair Operators:
    - Triệu chứng: Thuật toán kết thúc với một số khách hàng không được phục vụ, mặc dù có thể tạo tuyến mới cho họ.
    - Nguyên nhân: Các toán tử GreedyTimeInsertion và ParetoFocusRepair thiếu logic else để tạo tuyến mới khi không tìm thấy vị trí chèn nào trong các tuyến
      hiện có.
    - Fix: Bổ sung khối else để tạo Route mới cho khách hàng chưa được phục vụ, đảm bảo mọi khách hàng đều được chèn lại.

5. Lỗi "Log sai" cho Operator (Incorrect Operator Log):
    - Triệu chứng: File log _operators.csv luôn hiển thị Score và UsageCount bằng 0.
    - Nguyên nhân: Trong ALNSSolver, việc reset (resetScores) được gọi trước khi ghi log (logOperatorSegment), khiến logger đọc phải dữ liệu đã bị xóa.
    - Fix: Thay đổi thứ tự trong ALNSSolver: Luôn gọi logOperatorSegment trước, sau đó mới gọi updateWeights và resetScores.

6. Lỗi "Trạm sạc trùng lặp" (Duplicate Stations):
    - Triệu chứng: Một tuyến đường có nhiều lượt ghé cùng một trạm sạc một cách vô lý, thường là ở cùng một vị trí.
    - Nguyên nhân: Toán tử GreedyStationRepair xử lý từng khách hàng một cách độc lập. Nó có thể thêm một trạm sạc cho khách hàng A, và sau đó lại thêm một trạm
      sạc y hệt cho khách hàng B vì không nhận ra trạm sạc trước đó đã tồn tại.
    - Fix: Trong GreedyStationRepair, trước khi thử chèn một trạm sạc, kiểm tra xem node ngay trước hoặc sau vị trí định chèn có phải là cùng một trạm sạc đó
      không. Nếu có, bỏ qua nước đi đó.

6. ⏱️ Phân tích Độ phức tạp (Complexity Analysis)
> Ký hiệu: N là tổng số khách hàng, M là số xe, K là số node trung bình mỗi tuyến, d là số khách bị xóa.

A. Các hàm tính toán cốt lõi (Core Calculations)
- `Route::evaluate()`: `O(K)`
    - Thực hiện 1 lượt duyệt ngược (Backward Pass) và 1 lượt duyệt xuôi (Forward Pass) trên K node của tuyến. Mỗi bước là O(1). Rất hiệu quả.
- `Route::checkInsertionCost()`: `O(K)`
    - Tạo một tuyến mô phỏng có K+1 node và chạy lại logic tương tự evaluate(). Vẫn là tuyến tính theo số node của một route.

B. Các toán tử phá hủy (Destroy Operators)
- `UnifiedCostDestroy`: `O(N log N)`
    - Hợp nhất các logic (Distance, Energy, Time, Random) vào một flow duy nhất. Tính toán chi phí "saving" cho toàn bộ N node và sắp xếp.
- `ShawDestroy`: `O(d * N)`
    - Với mỗi khách hàng trong d khách hàng cần xóa, tính toán độ tương quan với ~N khách hàng còn lại.
- `InefficientRouteRemoval`: `O(M * K) = O(N)`
    - Xác định các tuyến hoạt động kém hiệu quả (ít khách, wait time lớn) để xóa bỏ. 
- `RouteMergingDestroy`: `O(M^2 * K)`
    - Tìm kiếm cặp tuyến có thể gộp được với nhau.

C. Các toán tử xây dựng (Repair Operators)
- `AdaptiveInsertion`: `O(d * N * K)`
    - Thay thế cho GreedyDistance/TimeInsertion. Tự động điều chỉnh trọng số (Weights) giữa Distance, Time và Energy để tìm vị trí chèn tốt nhất.
- `SmartStationRepair`: `O(d * N * K)`
    - Thay thế GreedyStationRepair. Tìm kiếm vị trí chèn trạm sạc tối ưu hơn, tránh lặp trạm.
- `ParetoFocusRepair` & `GreedyEnergyInsertion`: `O(d * N * K)`
- `RegretKRepair`: `O(d^2 * N * K)`
    - Vẫn là toán tử tốn kém nhất. Cần hạn chế sử dụng hoặc giảm K.

D. Local Search
- `searchRelocate` & `searchSwap`: `O(1)` hoặc `O(K)` (Fast Path) / `O(N^2 * K)` (Worst Case)
    - Nhờ **Pruning** và **Tiered Filtering**, chi phí trung bình giảm xuống đáng kể. Hầu hết các node chỉ kiểm tra KNN (k=3-5) và bị loại ngay ở bước Fast Check.
    - **Station-Assisted Repair:** Khi chèn khách thất bại do năng lượng, thuật toán thử chèn thêm trạm sạc (`O(K)`). Việc này tăng nhẹ chi phí nhưng giúp thoát khỏi bế tắc cục bộ (Local Optima).
- `runSmartMultiRouteMerge`: `O(M^2 * K)`
    - Duyệt qua cặp routes (M^2) và tái cấu trúc tuyến bằng insertion (`O(K)`). Do M nhỏ (số lượng xe), thao tác này rất nhanh.

E. Tổng quan vòng lặp ALNS
- Hiệu năng tổng thể được quyết định bởi tỷ lệ **Pruning thành công**. Nhờ các cơ chế Fast Check, thuật toán có thể duyệt hàng triệu moves mỗi giây mà vẫn đảm bảo tính chính xác của năng lượng và thời gian.
- **Vehicle Reduction** đóng vai trò quan trọng nhất trong việc giảm số xe, trong khi **Local Search** đóng vai trò tinh chỉnh distance.

E. Tư Duy Thuật Toán & Cơ Chế Tối Ưu (Algorithmic Concepts)
Để giải quyết bài toán EVRPTW phức tạp, hệ thống áp dụng các tư duy chiến lược sau:

1. **Smart Multi-Route Merge (Gộp tuyến thông minh - Pool & Reconstruct)**
   - **Vấn đề:** Các toán tử gộp truyền thống thường thất bại do ràng buộc năng lượng/thời gian chặt chẽ của xe điện.
   - **Ý tưởng:** Thay vì gộp mù quáng, thuật toán chọn 2 tuyến có không gian gần nhau (Centroid Distance), "phá hủy" hoàn toàn chúng để đưa khách hàng vào một **hồ chứa (pool)** chung. Sau đó, **xây dựng lại** tuyến mới từ hồ chứa này.
   - **Station-Assisted Strategy:** Nếu quá trình xây lại bị tắc do hết pin, thuật toán **chủ động chèn trạm sạc** vào vị trí thích hợp để "cứu" tuyến đường đó. Điều này giúp gộp được cả những tuyến dài mà bình thường không thể gộp.

2. **Smallest Route Elimination (Chiến lược "Diệt" tuyến nhỏ nhất)**
   - **Vấn đề:** ALNS thường có xu hướng cân bằng tải, khiến ta khó giảm bớt một xe cuối cùng (ví dụ: 10 xe đều chở ít khách thay vì 9 xe đầy).
   - **Ý tưởng:** Xác định tuyến "yếu nhất" (ít khách nhất) và **cưỡng ép giải tán** nó. Khách hàng của tuyến này được phân tán sang các tuyến khác. Nếu một khách không thể chèn vào đâu do thiếu pin, thuật toán sẽ thử chèn kèm một trạm sạc. Mục tiêu là hy sinh một chút Distance để đạt được mục tiêu tối thượng: Giảm số lượng xe.

3. **Tiered Filtering (Bộ lọc phễu đa tầng)**
   - **Vấn đề:** Hàm kiểm tra `evaluate()` (Deep Copy + Backward Pass) cực kỳ tốn kém (O(N)).
   - **Ý tưởng:** Sử dụng mô hình phễu 3 lớp để loại bỏ sớm các nước đi tồi:
     - *Tier 1 (Static):* Kiểm tra nhanh Capacity & Time Window bounds (O(1)).
     - *Tier 2 (Fast Check - Delta):* Tính toán thay đổi chi phí cục bộ (`checkInsertionCost`) mà không cần copy full route.
     - *Tier 3 (Full Eval):* Chỉ chạy `evaluate()` nặng nề cho < 5% số moves lọt qua 2 vòng đầu.

4. **Static Move Descriptor (SMD - Zero Allocation)**
   - **Vấn đề:** Việc tạo hủy hàng triệu object `Move` nhỏ trong vòng lặp gây phân mảnh bộ nhớ.
   - **Ý tưởng:** Khởi tạo sẵn (Pre-allocate) một đối tượng `MoveDescriptor` và tái sử dụng nó liên tục. Chỉ reset trạng thái thay vì cấp phát mới. Giúp CPU cache hoạt động hiệu quả hơn.


1. Độ phức tạp quá cao của toán tử RegretKRepair

* Vấn đề: Đây là "kẻ hủy diệt hiệu năng" lớn nhất trong các toán tử của bạn.
* Vị trí: src/alns/operators/repair/RegretKRepair.cpp
* Giải thích: Độ phức tạp của nó là O(d² * N * K). Vòng lặp while bên ngoài chạy d lần. Bên trong nó, vòng lặp for lại chạy qua ~d khách hàng còn lại. Với mỗi
  khách hàng, nó lại gọi findKBestInsertions (O(N*K)). Sự lồng nhau của các vòng lặp này tạo ra một độ phức tạp theo hàm số mũ, khiến nó cực kỳ chậm khi số
  lượng khách hàng cần chèn (d) tăng lên.
* Đề xuất:
    * Cách 1 (Đơn giản): Giảm tần suất sử dụng toán tử này. Trong ALNSSolver, bạn có thể cho nó một trọng số ban đầu thấp hơn.
    * Cách 2 (Tối ưu): Cải tiến lại thuật toán. Thay vì tính lại regret cho tất cả khách hàng mỗi lần, bạn có thể tính một lần, chèn khách hàng có regret cao
      nhất, sau đó chỉ cập nhật lại regret cho các khách hàng bị ảnh hưởng bởi sự thay đổi đó. Đây là một kỹ thuật phức tạp nhưng sẽ giảm độ phức tạp đáng kể.

2. Chi phí khổng lồ của Local Search (đặc biệt là searchRelocate)

* Vấn đề: Các toán tử tìm kiếm cục bộ, đặc biệt là searchRelocate, có độ phức tạp rất cao ở trường hợp xấu nhất.
* Vị trí: src/alns/LocalSearch.cpp
* Giải thích:
    * searchRelocate: O(N² * K). Nó cố gắng thử di chuyển mọi khách hàng đến mọi vị trí khả dĩ khác. Mặc dù bạn đã có heuristic areRoutesClose để giảm bớt, nó
      vẫn là một chi phí rất lớn.
    * searchTwoOpt: O(N * K²). Cũng rất tốn kém.
* Đề xuất:
    * Neighborhood Pruning (Cắt tỉa không gian tìm kiếm): Thay vì duyệt toàn bộ, chỉ xem xét các nước đi "có khả năng" nhất. Ví dụ, chỉ thử relocate một khách
      hàng đến các vị trí gần nó trong k khách hàng khác (k-nearest neighbors).
    * Giảm tần suất: Không gọi runDistanceOptimization ở mỗi vòng lặp của LocalSearch::run. Thay vào đó, có thể gọi nó một cách ngẫu nhiên hoặc khi thuật toán
      có dấu hiệu bị "mắc kẹt" (stagnation).

3. Sao chép đối tượng Solution trong mỗi vòng lặp

* Vấn đề: Dòng Solution s_new = this->s_current; ở đầu mỗi vòng lặp ALNS có thể rất tốn kém.
* Vị trí: src/alns/ALNSSolver.cpp
* Giải thích: Nếu hàm copy constructor của Solution thực hiện "deep copy" (sao chép sâu) tất cả các Route và dữ liệu bên trong, nó sẽ gây ra một lượng lớn thao
  tác cấp phát bộ nhớ ở mỗi một trong hàng chục nghìn vòng lặp.
* Đề xuất:
    * Kiểm tra Copy Constructor: Tôi cần xem Solution.cpp để xác nhận xem nó là deep copy hay shallow copy.
    * Tối ưu hóa: Nếu nó là deep copy, hãy xem xét việc không tạo s_new đầy đủ. Thay vào đó, s_new có thể chỉ lưu một tham chiếu đến s_current và một "delta"
      ghi lại những thay đổi. Khi cần, nó sẽ áp dụng delta đó để tạo ra trạng thái mới. Đây là một thay đổi lớn về kiến trúc nhưng mang lại hiệu quả cao. Một
      cách đơn giản hơn là đảm bảo Route được copy theo kiểu shallow copy (chỉ copy con trỏ và các biến cơ bản).

4. Sử dụng dynamic_pointer_cast trong vòng lặp

* Vấn đề: dynamic_pointer_cast được sử dụng lặp đi lặp lại trong các toán tử và local search để xác định loại của một Node.
* Vị trí: Rải rác trong các file toán tử và LocalSearch.cpp.
* Giải thích: dynamic_pointer_cast có chi phí thực thi (runtime overhead) vì nó cần kiểm tra thông tin type của đối tượng. Khi được gọi hàng triệu lần trong các
  vòng lặp lồng nhau, chi phí này sẽ tích tụ lại.
* Đề xuất:
    * Thêm một enum NodeType { CUSTOMER, STATION, DEPOT }; vào class Node cơ sở.
    * Trong constructor của Customer, Station, Depot, gán loại tương ứng cho biến thành viên này.
    * Sau đó, thay vì dùng dynamic_pointer_cast, bạn chỉ cần kiểm tra if (node->getType() == NodeType::CUSTOMER). Thao tác so sánh enum này nhanh hơn rất nhiều.

  ---

Tóm lại, để tối ưu tốc độ, bạn nên tập trung vào:
1. Giảm độ phức tạp của `RegretKRepair` và `LocalSearch` (ảnh hưởng lớn nhất).
2. Kiểm tra và tối ưu hóa việc sao chép `Solution` (ảnh hưởng trung bình).
3. Loại bỏ `dynamic_pointer_cast` (tối ưu hóa vi mô nhưng vẫn có giá trị).

---
### **Mô Hình Toán Học (Mathematical Model)**

Dựa trên logic trong `Route.cpp` và `Solution.cpp`, mô hình toán học cho bài toán EVRPTW được định nghĩa như sau:

**1. Tập Hợp (Sets)**

*   `N`: Tập hợp tất cả các điểm (nodes), `N = C ∪ S ∪ {0}`.
*   `C`: Tập hợp các khách hàng (customers), `C = {1, 2, ..., n}`.
*   `S`: Tập hợp các trạm sạc (stations).
*   `{0}`: Điểm depot (xuất phát và kết thúc).
*   `V`: Tập hợp các xe (vehicles), `V = {1, 2, ..., m}`.
*   `P_k`: Chuỗi các điểm (sequence of nodes) trên tuyến đường của xe `k`.

**2. Tham Số (Parameters)**

*   `d_ij`: Khoảng cách di chuyển từ điểm `i` đến điểm `j`.
*   `t_ij`: Thời gian di chuyển từ điểm `i` đến điểm `j`.
*   `q_i`: Nhu cầu (demand) của khách hàng `i`. `q_0 = 0`, `q_s = 0` với `s ∈ S`.
*   `[e_i, l_i]`: Cửa sổ thời gian (time window) tại điểm `i`, với `e_i` là thời gian sớm nhất (ready time) và `l_i` là thời gian muộn nhất (due date).
*   `s_i`: Thời gian phục vụ (service time) tại khách hàng `i`.
*   `Q`: Tải trọng tối đa (capacity) của xe.
*   `B`: Dung lượng pin tối đa (battery capacity) của xe.
*   `r`: Tỷ lệ tiêu thụ năng lượng (energy consumption rate) trên mỗi đơn vị khoảng cách.
*   `g`: Tỷ lệ sạc pin (charging rate) tại trạm sạc.

**3. Biến Quyết Định (Decision Variables)**

*   `x_ijk`: Biến nhị phân, `x_ijk = 1` nếu xe `k` đi trực tiếp từ điểm `i` đến điểm `j`, và `x_ijk = 0` nếu ngược lại.
*   `y_ik`: Biến nhị phân, `y_ik = 1` nếu điểm `i` được phục vụ bởi xe `k`, và `y_ik = 0` nếu ngược lại.
*   `A_ik`: Thời gian xe `k` đến điểm `i` (Arrival time).
*   `D_ik`: Thời gian xe `k` rời khỏi điểm `i` (Departure time).
*   `b_ik`: Mức pin của xe `k` khi đến điểm `i`.
*   `c_ik`: Lượng pin được sạc cho xe `k` tại điểm `i` (chỉ áp dụng cho `i ∈ S`).
*   `u_ik`: Tải trọng còn lại của xe `k` khi đến điểm `i`.

**4. Hàm Mục Tiêu (Objective Functions)**

Bài toán là tối ưu hóa đa mục tiêu (multi-objective), với các mục tiêu được sắp xếp theo thứ tự ưu tiên trong hàm `Solution::dominates`:

1.  **Minimize Total Vehicles:** Tối thiểu hóa tổng số xe sử dụng.
    `f1 = ∑_{k ∈ V} y_0k`
2.  **Minimize Total Distance:** Tối thiểu hóa tổng quãng đường di chuyển.
    `f2 = ∑_{i ∈ N} ∑_{j ∈ N} ∑_{k ∈ V} d_ij * x_ijk`
3.  **Minimize Workload Variance:** Tối thiểu hóa phương sai của thời gian hoạt động giữa các tuyến.
    `f3 = Var(T_k) = (1/|V_u|) * ∑_{k ∈ V_u} (T_k - μ_T)²`
4.  **Minimize Makespan (Max Time):** Tối thiểu hóa thời gian hoàn thành của tuyến đường dài nhất.
    `f4 = max_{k ∈ V} (A_{0k, end})`

**5. Ràng Buộc (Constraints)**

*   **Ràng buộc về luồng (Flow Constraints):**
    *   Mỗi khách hàng được phục vụ đúng một lần: `∑_{k ∈ V} y_ik = 1, ∀i ∈ C`.
    *   Mỗi xe xuất phát từ depot và kết thúc tại depot: `∑_{j ∈ N} x_{0jk} = y_0k, ∀k ∈ V` và `∑_{i ∈ N} x_{i0k} = y_0k, ∀k ∈ V`.
    *   Bảo toàn luồng: `∑_{i ∈ N} x_{ijk} = ∑_{j ∈ N} x_{jik} = y_ik, ∀i ∈ C ∪ S, ∀k ∈ V`.

*   **Ràng buộc về tải trọng (Capacity Constraint):**
    *   Tải trọng của xe không được vượt quá giới hạn: `u_jk = u_ik - q_j` nếu `x_ijk = 1`. `0 ≤ u_ik ≤ Q`.

*   **Ràng buộc về cửa sổ thời gian (Time Window Constraints):**
    *   Thời gian đến phải nằm trong cửa sổ thời gian: `e_i ≤ A_ik ≤ l_i, ∀i ∈ N, ∀k ∈ V`.
    *   Tính toán thời gian rời đi:
        *   Tại khách hàng: `D_ik = max(A_ik, e_i) + s_i`.
        *   Tại trạm sạc: `D_ik = max(A_ik, e_i) + (c_ik * g)`.
    *   Liên kết thời gian giữa các điểm: `A_jk = D_ik + t_ij` nếu `x_ijk = 1`.

*   **Ràng buộc về năng lượng (Energy Constraints):**
    *   Mức pin khi đến điểm `j` từ `i`: `b_jk = b_ik' - (d_ij * r)`, với `b_ik'` là pin sau khi sạc tại `i`.
    *   Pin không bao giờ âm: `b_ik ≥ 0, ∀i ∈ N, ∀k ∈ V`.
    *   Pin không vượt quá dung lượng tối đa: `b_ik + c_ik ≤ B, ∀i ∈ S, ∀k ∈ V`.
    *   Logic sạc tối ưu (Backward Pass trong `Route::evaluate`): Lượng pin sạc `c_ik` được tính toán để đảm bảo xe có đủ năng lượng tối thiểu cần thiết để hoàn thành phần còn lại của tuyến đường.