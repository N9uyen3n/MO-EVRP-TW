# Trình giải bài toán MO-EVRP-TW

Dự án này cung cấp một trình giải (solver) được viết bằng C++ cho **Bài toán Định tuyến Phương tiện điện Đa mục tiêu với Cửa sổ Thời gian (Multi-Objective Electric Vehicle Routing Problem with Time Windows - MO-EVRP-TW)**.

Thuật toán cốt lõi được sử dụng là **Adaptive Large Neighborhood Search (ALNS)**, một metaheuristic mạnh mẽ để giải quyết các bài toán tối ưu hóa tổ hợp.

## 1. Mô tả chi tiết bài toán (dựa trên file MOEVRP.pdf)

Bài toán được giải trong dự án này là **Bài toán Định tuyến Phương tiện điện Đa mục tiêu với Cửa sổ Thời gian (Multi-Objective Electric Vehicle Routing Problem with Time Windows - MO-EVRP-TW)**. Dựa trên tài liệu PDF đính kèm, mô hình toán học của bài toán được định nghĩa trên một đồ thị `G = (V, A)` và bao gồm các thành phần, ràng buộc và mục tiêu sau:

### 1.1. Các thành phần chính

- **Đồ thị:** Bao gồm một tập các đỉnh (nodes) và cung (arcs).
  - **Depot (`{0}`):** Một kho duy nhất là điểm xuất phát và kết thúc của mọi phương tiện. Tương ứng với lớp `Depot` trong mã nguồn.
  - **Khách hàng (`N`):** Tập hợp các khách hàng cần được phục vụ. Mỗi khách hàng có một nhu cầu (`demand`), một cửa sổ thời gian phục vụ (`readyTime`, `dueDate`), và thời gian phục vụ (`serviceTime`). Tương ứng với lớp `Customer`.
  - **Trạm sạc (`F`):** Tập hợp các trạm sạc công cộng nơi xe có thể sạc lại pin (cho phép sạc một phần - partial recharge). Tương ứng với lớp `Station`.
- **Đội xe:** Một đội xe điện đồng nhất, mỗi xe có các thuộc tính:
  - **Tải trọng tối đa (`C`):** Lượng hàng tối đa xe có thể chở. Tương ứng với `vehicleCapacity` trong lớp `Instance`.
  - **Dung lượng pin tối đa (`Q`):** Năng lượng tối đa xe có thể lưu trữ. Tương ứng với `vehicleBattery` trong lớp `Instance`.
  - **Tốc độ tiêu thụ năng lượng (`h`):** Năng lượng tiêu thụ trên mỗi đơn vị khoảng cách. Tương ứng với `vehicleEnergyRate` trong lớp `Instance`.

### 1.2. Các ràng buộc tổng quan (Constraints)

Một lời giải (tập hợp các tuyến đường) được coi là khả thi nếu nó thỏa mãn tất cả các ràng buộc sau:

1.  **Ràng buộc luồng (Flow):** Mỗi khách hàng phải được phục vụ bởi đúng một xe và đúng một lần.
2.  **Ràng buộc tải trọng (Capacity):** Tổng khối lượng hàng hóa trên xe không bao giờ được vượt quá tải trọng tối đa `C`.
3.  **Ràng buộc cửa sổ thời gian (Time Windows):** Xe phải đến phục vụ khách hàng trong khoảng thời gian cho phép `[readyTime, dueDate]`.
4.  **Ràng buộc năng lượng (Energy):** Lượng pin của xe không bao giờ được dưới 0. Xe phải đến được trạm sạc hoặc quay về depot trước khi hết pin.
5.  **Ràng buộc thời gian làm việc của tài xế:** Tổng thời gian của một tuyến đường (từ lúc rời kho đến lúc quay về) không được vượt quá một ngưỡng cho phép.

### 1.3. Các hàm mục tiêu (Objectives)

Bài toán là đa mục tiêu, tức là cần tối ưu hóa đồng thời nhiều mục tiêu xung đột nhau. Dựa trên file PDF, mô hình này tối thiểu hóa một tập hợp 4 hàm mục tiêu:

- **`Z1`: Số lượng xe sử dụng.**
- **`Z2`: Tổng quãng đường di chuyển.**
- **`Z3`: Tổng chi phí năng lượng (hoặc tổng lượng pin đã sạc).**
- **`Z4`: Thời gian làm việc dài nhất (Makespan).**

### 1.4. Mô hình toán học chi tiết

Phần này diễn giải mô hình toán học được trình bày trong file `MOEVRP.pdf` và cách nó được ánh xạ vào trong mã nguồn.

#### Ký hiệu và Tham số

| Ký hiệu | Diễn giải trong PDF | Ánh xạ trong Code |
| :--- | :--- | :--- |
| `d_ij` | Khoảng cách giữa đỉnh `i` và `j` | Tính toán động bằng hàm `Utils::distance` |
| `t_ij` | Thời gian di chuyển giữa `i` và `j` | `d_ij / Instance::vehicleVelocity` |
| `C` | Sức chứa của xe | `Instance::vehicleCapacity` |
| `Q` | Dung lượng pin | `Instance::vehicleBattery` |
| `h` | Tốc độ tiêu hao pin | `Instance::vehicleEnergyRate` |
| `g` | Tốc độ sạc | `Station::chargingRate` |
| `q_i` | Nhu cầu tại đỉnh `i` | `Customer::demand` |
| `[e_i, l_i]` | Cửa sổ thời gian tại đỉnh `i` | `Customer::readyTime`, `Customer::dueDate` |
| `s_i` | Thời gian phục vụ tại đỉnh `i` | `Customer::serviceTime` |
| `x_ij` | Biến nhị phân: 1 nếu xe đi từ `i` đến `j` | **Ngầm định**: Nếu `Node` `j` theo sau `Node` `i` trong `std::vector` của lớp `Route`, `x_ij = 1`. |
| `T_i` | *Biến quyết định*: Thời điểm đến đỉnh `i` | `RouteInfo::arrival_time` |
| `U_i` | *Biến quyết định*: Tải trọng khi đến `i` | `RouteInfo::arrival_load` |
| `Y_i` | *Biến quyết định*: Lượng pin khi đến `i` | `RouteInfo::arrival_battery` |

---

#### Các hàm mục tiêu (Trang 6, PDF)

**1. Số lượng xe sử dụng (Z1)**
`min Z1 = sum(x_0j)` for `j` in Customers `U` Stations
- **Ý nghĩa:** Tối thiểu hóa số xe được sử dụng.
- **Liên hệ với code:** Mục tiêu này tương đương với việc tối thiểu hóa số lượng đối tượng `Route` trong một `Solution`. `Z1 = solution.routes.size()`.

**2. Tổng quãng đường di chuyển (Z2)**
`min Z2 = sum(d_ij * x_ij)` for all `i, j`
- **Ý nghĩa:** Tối thiểu hóa tổng quãng đường của tất cả các xe.
- **Liên hệ với code:** Được tính bằng cách duyệt qua tất cả các `Route` trong `Solution`, và với mỗi `Route`, tính tổng khoảng cách giữa các `Node` liên tiếp trong hành trình.

**3. Tổng chi phí năng lượng (Z3)**
`min Z3 = sum(z_i)` for `i` in Stations
- **Ý nghĩa:** Tối thiểu hóa tổng lượng pin được sạc tại tất cả các trạm. `z_i` là lượng pin sạc tại trạm `i`.
- **Liên hệ với code:** Được tính bằng cách tính tổng của `(departure_battery - arrival_battery)` tại tất cả các `Node` là `Station` trong `Solution`.

**4. Thời gian làm việc dài nhất (Makespan) (Z4)**
`min Z4 = max(T_j)` for `j` in ending Depots
- **Ý nghĩa:** Tối thiểu hóa thời gian hoạt động của tuyến đường dài nhất.
- **Liên hệ với code:** Được tính bằng cách tìm giá trị `arrival_time` lớn nhất tại `Node` depot cuối cùng trên tất cả các `Route`.

---

#### Các ràng buộc chi tiết (Trang 7-8, PDF)

**1. Ràng buộc về Luồng (Flow Constraints)**
- **(12)** `sum(x_ij) = 1` (cho mỗi khách hàng `i`): Mỗi khách hàng được phục vụ đúng 1 lần.
- **(13)** `sum(x_ij) = sum(x_jk)` (cho mỗi điểm `j`): Nếu một xe đi vào một điểm, nó phải đi ra.
- **(14)** `sum(x_0j) <= |E|`: Số xe sử dụng không vượt quá số xe có sẵn.
- **Liên hệ với code:** Các ràng buộc này được đảm bảo bởi cấu trúc dữ liệu. Một `Solution` là một tập hợp các `Route`. Các thuật toán `destroy` và `repair` của ALNS được thiết kế để luôn duy trì tính hợp lệ này (một khách hàng chỉ thuộc về một `Route` tại một thời điểm, và mỗi `Route` là một chuỗi hợp lệ bắt đầu và kết thúc tại depot).

**2. Ràng buộc về Tải trọng (Capacity Constraints)**
- **(17)** `U_j <= U_i - q_i + C * (1 - x_ij)`: Tải trọng khi đến đỉnh `j` (`U_j`) phải nhỏ hơn hoặc bằng tải trọng khi rời đỉnh `i` (`U_i - q_i`).
- **(18)** `q_i <= U_i <= C`: Tải trọng khi đến một khách hàng phải lớn hơn hoặc bằng nhu cầu của khách hàng đó, và không bao giờ vượt quá sức chứa của xe.
- **Liên hệ với code:** Logic này được kiểm tra bên trong phương thức `Route::canInsert` và được cập nhật trong `Route::recalculateFrom`. Các trường `arrival_load` và `departure_load` trong `RouteInfo` theo dõi giá trị này. `canInsert` sẽ trả về `false` nếu một trong các điều kiện này bị vi phạm.

**3. Ràng buộc về Thời gian (Time Constraints)**
- **(20)** `T_j >= T_i + s_i + t_ij`: Thời gian đến đỉnh `j` (`T_j`) phải sau khi rời đỉnh `i` (thời điểm `T_i + s_i`) cộng với thời gian di chuyển (`t_ij`).
- **(21)** `T_j >= T_i + s_i + z_i/g + t_ij`: Tương tự (20) nhưng áp dụng khi rời trạm sạc, tính thêm thời gian sạc (`z_i/g`).
- **(22)** `e_i <= T_i <= l_i`: Thời gian đến một đỉnh phải nằm trong cửa sổ thời gian của đỉnh đó.
- **Liên hệ với code:** Tương tự ràng buộc tải trọng, logic này là cốt lõi của các phương thức kiểm tra tính khả thi trong lớp `Route`. Các trường `arrival_time` và `departure_time` trong `RouteInfo` theo dõi giá trị này.

**4. Ràng buộc về Năng lượng (Energy Constraints)**
- **(23)** `Y_j <= Y_i - h * d_ij + Q * (1 - x_ij)`: Lượng pin khi đến `j` (`Y_j`) bằng lượng pin khi đến `i` (`Y_i`) trừ đi năng lượng tiêu thụ (`h * d_ij`).
- **(25)** `Y_i >= h * d_ij`: Lượng pin khi đến `i` phải đủ để đi đến `j`.
- **(27)** `Y_i + z_i <= Q`: Lượng pin sau khi sạc không được vượt quá dung lượng tối đa.
- **(28)** `0 <= Y_i <= Q`: Lượng pin luôn phải nằm trong khoảng `[0, Q]`.- **Liên hệ với code:** Đây cũng là một phần quan trọng của việc kiểm tra tính khả thi trong lớp `Route`. Các trường `arrival_battery` và `departure_battery` trong `RouteInfo` theo dõi giá trị này. `canInsert` sẽ trả về `false` nếu xe không đủ pin để đến điểm tiếp theo.

## 2. Cấu trúc thư mục chi tiết

Việc phân chia code thành các thư mục riêng biệt giúp quản lý dự án hiệu quả, tách biệt giữa giao diện (cách các thành phần tương tác) và phần triển khai (logic thực sự).

- **`CMakeLists.txt`**: "Bản đồ" của dự án. File này chỉ dẫn cho công cụ `CMake` cách biên dịch mã nguồn, liên kết các thư viện cần thiết và tạo ra file thực thi cuối cùng.
- **`include/`**: Thư mục giao diện (Interfaces). Chứa các file header (`.h`) chỉ định "cái gì" một lớp có thể làm, nhưng không phải "làm như thế nào". Chúng định nghĩa cấu trúc của các lớp, các hàm thành viên và các biến mà các phần khác của chương trình có thể truy cập.
- **`src/`**: Thư mục triển khai (Implementations). Chứa các file mã nguồn (`.cpp`) cung cấp logic chi tiết cho các hàm và lớp đã được khai báo trong thư mục `include`.
- **`data/`**: Chứa các "bài toán" dưới dạng file văn bản. Trình giải sẽ đọc các file này để lấy dữ liệu đầu vào và tìm lời giải. Bộ `solomon` là một bộ dữ liệu benchmark chuẩn trong giới nghiên cứu VRP.
- **`build/`**: Thư mục sản phẩm. Thư mục này ban đầu không có; nó được tạo ra bởi CMake trong quá trình build. Toàn bộ kết quả biên dịch (object files, file thực thi `mo_evrp_tw`) sẽ nằm ở đây để giữ cho thư mục chính luôn sạch sẽ.
- **`python/`**: Thư mục công cụ phụ trợ. Chứa các script Python có thể được dùng cho các tác vụ như phân tích dữ liệu kết quả, hoặc vẽ bản đồ các tuyến đường đã được tối ưu hóa để trực quan hóa lời giải.

## 3. Phân tích chi tiết mã nguồn

Kiến trúc phần mềm được xây dựng theo hướng đối tượng, trong đó mỗi thực thể của bài toán được ánh xạ vào một lớp C++.- **`Parser` (`Parser.h`, `Parser.cpp`)**
  - **Chức năng:** Đóng vai trò là "bộ nạp dữ liệu". Nó chứa một hàm `static` duy nhất là `parse`, có nhiệm vụ đọc file văn bản theo định dạng Solomon, trích xuất thông tin về xe, depot, khách hàng, trạm sạc và tạo ra một đối tượng `Instance` hoàn chỉnh.

- **`Instance` (`Instance.h`, `Instance.cpp`)**
  - **Chức năng:** Là một "container" chứa toàn bộ dữ liệu của một bài toán cụ thể. Sau khi được `Parser` tạo, đối tượng `Instance` sẽ giữ một danh sách tất cả các `Node` (khách hàng, trạm sạc, depot) và các thông số chung của đội xe (tải trọng, dung lượng pin...). Đối tượng này là bất biến (read-only) trong suốt quá trình giải.

- **Hệ thống `Node` (`Node.h`, `Customer.h`, `Station.h`, `Depot.h`)**
  - **Chức năng:** Mô hình hóa các địa điểm trên bản đồ bằng kế thừa.
  - **`Node`**: Lớp cha (base class) chứa các thuộc tính cơ bản nhất mà mọi địa điểm đều có: `id` (mã định danh) và tọa độ `(x, y)`.
  - **`Customer`**: Lớp con kế thừa từ `Node`, bổ sung các thuộc tính riêng cho khách hàng: `demand` (lượng hàng yêu cầu), `readyTime` và `dueDate` (cửa sổ thời gian), và `serviceTime` (thời gian trả hàng).
  - **`Station`**: Lớp con kế thừa từ `Node`, bổ sung `chargingRate` (tốc độ sạc).
  - **`Depot`**: Lớp con kế thừa từ `Node`, đại diện cho kho trung tâm, nơi bắt đầu và kết thúc của các tuyến đường.

- **`Vehicle` (`Vehicle.h`, `Vehicle.cpp`)**
  - **Chức năng:** Đóng vai trò là một "bản thiết kế" (template) cho các phương tiện. Nó chứa các thông số kỹ thuật cố định của một loại xe: `capacity` (tải trọng tối đa), `batteryCapacity` (dung lượng pin tối đa), và `energyConsumptionRate` (mức tiêu thụ năng lượng trên mỗi đơn vị khoảng cách).

- **`Route` và `RouteInfo` (`Route.h`, `RouteInfo.h`, ...)**
  - **Chức năng:** Mô hình hóa hành trình của một chiếc xe.
  - **`RouteInfo`**: Là một `struct` đơn giản, ghi lại "nhật ký" trạng thái của xe tại một điểm dừng: nó là `Node` nào, thời gian đến, lượng pin khi đến, lượng hàng khi đến, và các thông số tương tự khi rời đi.
  - **`Route`**: Là một tuyến đường hoàn chỉnh, về cơ bản là một danh sách (`vector`) các `RouteInfo`. Lớp này chứa các logic quan trọng để kiểm tra tính khả thi (ví dụ: `canInsert`) khi thêm một khách hàng mới vào tuyến đường, đảm bảo không vi phạm các ràng buộc về tải trọng, thời gian và năng lượng.

- **`Solver` và `ALNS` (`Solver.h`, `ALNS.h`, ...)**
  - **Chức năng:** Là "bộ não" của toàn bộ chương trình.
  - **`Solver`**: Là một lớp trừu tượng (abstract class) định nghĩa giao diện chung cho mọi thuật toán giải. Nó yêu cầu bất kỳ lớp con nào cũng phải có một phương thức `solve`.
  - **`ALNS`**: Là lớp triển khai cụ thể của `Solver`. Nó chứa logic của thuật toán Adaptive Large Neighborhood Search. Vòng lặp chính của nó liên tục:
    1.  **Phá hủy (Destroy):** Xóa một phần giải pháp hiện tại (ví dụ: xóa một vài khách hàng khỏi các tuyến đường).
    2.  **Sửa chữa (Repair):** Tìm cách chèn lại các khách hàng đã xóa vào các vị trí tốt nhất có thể trong các tuyến đường để tạo ra một giải pháp mới.
    - Thuật toán này duy trì một "kho lưu trữ" (`archive`) các giải pháp tốt nhất (Pareto Front) đã tìm thấy và tự động điều chỉnh việc lựa chọn các toán tử "destroy" và "repair" để tối ưu hóa quá trình tìm kiếm.

## 4. Cách Build và Chạy

### Yêu cầu
- C++ compiler (hỗ trợ C++17)
- CMake (phiên bản 3.10 trở lên)

### Các bước Build
1.  Mở terminal và điều hướng đến thư mục gốc của dự án (`MO-EVRP-TW`).
2.  Tạo một thư mục build và di chuyển vào đó:
    ```bash
    mkdir -p build
    cd build
    ```
3.  Chạy CMake để cấu hình project:
    ```bash
    cmake ..
    ```
4.  Biên dịch mã nguồn:
    ```bash
    make
    ```
    Sau khi hoàn tất, một file thực thi có tên `mo_evrp_tw` sẽ được tạo trong thư mục `build`.

### Chạy trình giải
File thực thi nhận một tham số duy nhất là đường dẫn đến file dữ liệu (instance).

**Cú pháp:**
```bash
./build/mo_evrp_tw <đường_dẫn_tới_file_dữ_liệu>
```

**Ví dụ:**
```bash
./build/mo_evrp_tw data/solomon/c101_21.txt
```

Chương trình sẽ đọc file instance, chạy thuật toán ALNS, và in ra tập hợp các giải pháp Pareto Front tìm được.

## 5. Ý tưởng thuật toán ALNS (Adaptive Large Neighborhood Search)

Thuật toán ALNS là một metaheuristic, tức là một chiến lược tìm kiếm bậc cao, được điều chỉnh để giải quyết bài toán tối ưu hóa này. Ý tưởng chính không phải là tìm kiếm một cách toàn cục (brute-force) mà là **liên tục cải thiện một giải pháp tốt hiện có** thông qua các bước phá hủy và sửa chữa.

Dựa trên giả mã trong file PDF (trang 8-9), luồng hoạt động của thuật toán trong dự án này như sau:

### 5.1. Khởi tạo

1.  **Tạo giải pháp ban đầu (`GenerateInitialSolution`):** Bắt đầu bằng một giải pháp khả thi (nhưng có thể chưa tốt). Ví dụ: mỗi khách hàng một chuyến xe riêng.
2.  **Khởi tạo Archive:** Tạo một "kho lưu trữ" (`archive`) và đưa giải pháp ban đầu vào đó. `Archive` này chính là tập hợp các giải pháp Pareto Front sẽ được trả về cuối cùng.
3.  **Khởi tạo toán tử và trọng số:**
    -   Định nghĩa một tập các toán tử **Phá hủy (Destroy)**, ví dụ: xóa ngẫu nhiên vài khách hàng, xóa những khách hàng đắt nhất...
    -   Định nghĩa một tập các toán tử **Sửa chữa (Repair)**, ví dụ: chèn lại khách hàng vào vị trí rẻ nhất, chèn tham lam...
    -   Gán cho mỗi toán tử một **trọng số** (`weight`) và **điểm số** (`score`) ban đầu. Trọng số quyết định xác suất một toán tử được chọn.

### 5.2. Vòng lặp chính

Thuật toán lặp lại các bước sau cho đến khi một điều kiện dừng được thỏa mãn (ví dụ: hết thời gian hoặc số vòng lặp tối đa):

1.  **Chọn toán tử:** Dựa trên trọng số, chọn một toán tử `destroy` và một toán tử `repair`.
    > Ví dụ: `d = SelectOperator(D, wd)`, `r = SelectOperator(R, wr)`.

2.  **Phá hủy:** Áp dụng toán tử `destroy` đã chọn để loại bỏ một số khách hàng khỏi giải pháp hiện tại (`S_current`), tạo ra một giải pháp không hoàn chỉnh.

3.  **Sửa chữa:** Áp dụng toán tử `repair` để chèn lại các khách hàng đã bị xóa vào giải pháp, tạo ra một giải pháp mới (`S_new`).

4.  **Đánh giá giải pháp mới:**
    -   Kiểm tra tính khả thi (`IsFeasible`) của `S_new`.
    -   Nếu khả thi, tính toán các giá trị hàm mục tiêu (`CalculateObjectives`).

5.  **Cập nhật giải pháp và Archive:**
    -   So sánh `S_new` với `S_current` và với các giải pháp trong `Archive`.
    -   **Nếu `S_new` trội hơn (dominates) `S_current`:** Chấp nhận `S_new` làm giải pháp hiện tại cho vòng lặp tiếp theo.
    -   **Nếu `S_new` không bị trội (non-dominated):** Cũng có thể chấp nhận `S_new` làm giải pháp hiện tại.
    -   **Nếu `S_new` bị trội (dominated):** Vẫn có một xác suất nhỏ để chấp nhận `S_new`. Cơ chế này (gọi là **Simulated Annealing - SA**) giúp thuật toán thoát khỏi các điểm tối ưu cục bộ (local optima). Xác suất này giảm dần theo thời gian.
    -   Cập nhật `Archive`: Nếu `S_new` không bị trội bởi bất kỳ giải pháp nào trong `Archive`, nó sẽ được thêm vào. Bất kỳ giải pháp nào trong `Archive` mà bị `S_new` trội hơn sẽ bị loại bỏ.

6.  **Cập nhật trọng số (Tính "Thích ứng" - Adaptive):**
    -   Sau mỗi một số vòng lặp nhất định (một "segment"), thuật toán sẽ cập nhật điểm số cho các toán tử đã dùng.
    -   Nếu một cặp toán tử `(d, r)` tạo ra một giải pháp tốt (ví dụ: cải thiện `Archive`), chúng sẽ được cộng nhiều điểm.
    -   Dựa trên điểm số mới, trọng số của các toán tử sẽ được cập nhật (`UpdateWeights`). Các toán tử hiệu quả hơn sẽ có trọng số cao hơn và do đó có nhiều khả năng được chọn hơn trong tương lai.

Quá trình này lặp đi lặp lại, giúp cho việc tìm kiếm ngày càng tập trung vào các vùng hứa hẹn hơn trong không gian lời giải, và "học" được các toán tử nào là tốt nhất cho bài toán cụ thể đang giải.
