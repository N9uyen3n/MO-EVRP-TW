# Bản đồ mã nguồn và luồng xử lý

Tài liệu mô tả giao diện thí nghiệm đang dùng trên `first` và `first-noEq`.
Các chương trình thử nghiệm lịch sử trong `src/test` không phải entry point
mặc định. Hướng dẫn lệnh ở [HUONG_DAN_CHAY.md](HUONG_DAN_CHAY.md), tham số ở
[THAM_SO.md](THAM_SO.md).

## 1. Luồng từ lệnh chạy đến kết quả

```text
run.py
  -> tools/experiments.py: kiểm tra CLI/config/input/binary, lập lịch các job
  -> mỗi job: snapshot input + config + metadata, khởi chạy một process C++
  -> src/app/ExperimentRunner.cpp: đọc config/seed và parse instance
  -> ALNSSolver: khởi tạo -> destroy/repair -> LS -> acceptance/archive
  -> xuất objectives và lịch trình thực tế của mọi lời giải trong archive
  -> ScheduleValidator.h: replay C++
  -> tools/validate.py: replay Python độc lập từ input + schedules
  -> cập nhật run.json, representative.json và summary.csv của benchmark
```

Launcher nhận JSON phẳng, merge defaults và tạo `parameters.cfg` dạng key=value
cho executable. Không nên gọi executable trực tiếp khi làm thí nghiệm vì sẽ
bỏ qua snapshot, metadata, kiểm định Python và quản lý benchmark.

## 2. File và trách nhiệm

| Vị trí | Trách nhiệm |
|---|---|
| `run.py` | Entry point Python gọn |
| `tools/experiments.py` | CLI, kiểm tra cấu hình, lập lịch tiến trình, log, metadata, summary |
| `tools/validate.py` | Parser độc lập và replay lịch trình đã lưu |
| `tools/test_experiments.py` | Kiểm tra chức năng giao diện và checker bằng dữ liệu nhỏ |
| `configs/default.json`, `smoke.json`, `variant.json` | Defaults, override kiểm tra nhanh, định danh variant |
| `CMakeLists.txt` | Build library/executable, ép chính sách variant theo nhánh |
| `src/app/ExperimentRunner.cpp` | CLI nội bộ C++, ánh xạ config, gọi solver, xuất lịch trình/objectives và kiểm định |
| `src/app/ExperimentDiagnostics.h` | Xuất thống kê timing/cache/LS/charging |
| `src/io/Parser.cpp`, `include/io/Parser.h` | Đọc dữ liệu Solomon EVRPTW cho solver |
| `src/core/Instance.cpp`, `include/core/Instance.h` | Node, tham số xe, khoảng cách/thời gian |
| `src/core/Route.cpp`, `include/core/Route.h` | Thứ tự ghé, tải/pin, sạc và đánh giá lịch trình tuyến |
| `src/core/Solution.cpp`, `include/core/Solution.h` | Tập tuyến, mục tiêu và tính khả thi cấp lời giải |
| `src/alns/ALNSSolver.cpp`, `include/alns/ALNSSolver.h` | Config, RNG, operator pools, vòng ALNS, SA, perturbation và thống kê |
| `src/alns/LocalSearch.cpp`, `include/alns/LocalSearch.h` | VND/neighborhoods, sàng lọc move, đánh giá và áp dụng move |
| `include/alns/SearchObjectives.h` | Chiếu mục tiêu theo chính sách Full/NoEQ |
| `src/alns/ParetoArchive.cpp` | Archive không bị trội theo các trục tìm kiếm |
| `src/alns/SolutionPool.cpp`, `ScatterSearch.cpp` | Pool và pha phối hợp lời giải tùy chọn |
| `src/alns/operators/destroy/` | Các toán tử loại khách/tuyến |
| `src/alns/operators/repair/` | Các toán tử chèn lại và xây dựng tuyến có sạc |
| `include/alns/ReconstructionCache.h`, `OrOptBlockCache.h`, `EjectionUndo.h` | Cache/hoàn tác phục vụ hiệu năng |
| `include/alns/LocalSearchProfile.h` | Bộ đếm và thời gian các vùng LS |
| `src/logger/ComprehensiveLogger.cpp` | Log tiến trình, từng vòng và front cuối |
| `src/test/ScheduleValidator.h` | Kiểm định replay lịch trình C++ |

## 3. Mô hình EVRPTW-PR đang giải

Mỗi khách được giao toàn bộ nhu cầu đúng một lần. Xe rời depot rồi trở về depot,
có giới hạn tải và pin; không có pickup. Trên cung i→j:

```text
distance = Euclidean(i,j)
travel_time = distance / v
energy_used = r * distance
charge_time = g * charge_amount
```

Đến khách sớm thì chờ, phục vụ trong cửa sổ thời gian theo quy tắc của checker;
đến trạm được sạc một phần, không bắt buộc đầy pin. Pin phải đủ để **đến trạm**,
không cho phép dùng lượng sạc sau khi đến để bù pin âm trên cung trước.
Lịch trình xuất phải tự thỏa thời gian sạc và giờ đóng cửa.

Mục tiêu được báo cáo:

- Z1: số xe/tuyến phục vụ khách, ưu tiên fleet size.
- Z2: tổng chiều dài các tuyến.
- Z3: Gini của workload active mỗi tuyến; active = thời lượng tuyến trừ tổng
  thời gian chờ, vẫn gồm thời gian chạy/phục vụ/sạc.
- Z4: thời lượng tuyến lớn nhất, có tính thời gian chờ.

Với workload a_i, Gini = sum_i sum_j |a_i-a_j| / (2*m*sum_i a_i), bằng 0
khi tổng workload gần 0 theo dung sai checker. Khi fleet size cố định, Full
lưu archive theo (Z2,Z3,Z4); NoEQ theo (Z2,Z4).

## 4. Vòng ALNS và toán tử thực sự được đăng ký

Solver tạo lời giải ban đầu, sau đó chọn destroy và repair bằng roulette theo
trọng số thích nghi. Mỗi vòng tạo ứng viên, xóa một phần khách, chèn lại, đánh
giá tính khả thi, gọi LS theo xác suất nếu được bật, rồi xét acceptance/archive.
Điểm và usage cập nhật trọng số theo segment. Có logic giảm fleet, đổi hướng
trọng số và perturbation khi trì trệ; điều kiện dừng còn gồm HV, thời gian, số
vòng. Đọc nhánh điều khiển trong `ALNSSolver::solve` khi cần diễn giải cụ thể.

Không phải mọi file operator trong repository đều đang chạy. Constructor
`ALNSSolver` hiện đăng ký:

| Nhóm | Toán tử | Trọng số khởi đầu |
|---|---|---:|
| Destroy | EnergyCriticalRemoval | 2.5 |
| Destroy | RouteMergingDestroy | 3.0 |
| Destroy | TimeSlackDestroy | 2.5 |
| Destroy | RandomRemoval | 2.0 |
| Destroy | VehicleReductionAwareDestroy | 3.5 |
| Repair | ChargingAwareRouteBuilder | 2.0 |
| Repair | AdaptiveInsertion | 2.0 |
| Repair | GreedyEnergyInsertion | 1.5 |
| Repair | SmartStationRepair | 2.5 |
| Repair | RegretKRepair, K=3 | 2.5 |

Các đăng ký đang comment, ví dụ VehiclePackingRepair hay UnifiedCostDestroy,
không thuộc pool chính chỉ vì file source tồn tại. Khởi tạo/tìm kiếm phụ có thể
có đường gọi riêng; bảng trên mô tả pool roulette của constructor.

## 5. Local search và move descriptor

`LocalSearch.cpp` có các neighborhood Relocate, Swap, OrOpt, TwoOpt,
InterTwoOpt, IntraOrOpt, CrossExchange, OrOptReversed. VND thử các neighborhood,
áp dụng cải thiện và quay lại vùng tìm kiếm theo điều khiển trong hàm run.
OrOpt dùng block khách độ dài 1, 2, 3. Không diễn giải LS như một lần quét duy
nhất hoặc intensity như số move.

Một lần `LocalSearch::run` có vòng lặp ba pha: VND tối ưu khoảng cách; dọn/
tối ưu trạm sạc theo chu kỳ; rồi thử giảm xe khi có nhiều tuyến và thỏa điều
kiện gọi. Pha giảm xe có các hàm ejection chain, loại tuyến nhỏ và gộp tuyến;
đây là logic riêng ngoài tám neighborhood VND. Có early stop và điều chỉnh
kích thước tập ứng viên; tần suất giảm xe còn chịu `vehicleReductionFreq_`.
Các hằng số nội bộ này chưa phải tham số của JSON công khai.

MoveDescriptor biểu diễn phép sửa tuyến; các hàm search dùng descriptor cục bộ,
lọc ứng viên bằng dữ liệu tĩnh, khoảng cách, tải và bound thời gian trước khi
tái dựng/đánh giá tuyến đắt hơn. `evaluateMove` đánh giá ứng viên;
`applyMove` áp dụng move được chọn. Cache phải gắn đúng trạng thái tuyến và ngữ
cảnh trọng số; đổi tuyến/ngữ cảnh cần vô hiệu hóa hoặc đổi khóa tương ứng.
Không suy tính khả thi chỉ từ delta khoảng cách: năng lượng, sạc và TW phải
được kiểm tra trên lịch trình kết quả.

`local_search_profile.json` cho biết neighborhood nào tốn thời gian, số lượt
thử/cải thiện; cache JSON cho biết hoạt động tối ưu. Các bộ đếm này phục vụ đo
hiệu năng, không thay cho file kiểm định tính khả thi.

## 6. Full và NoEQ khác nhau ở đâu

`SearchObjectives.h` tập trung chính sách mục tiêu: Full dùng Gini thật trong
quyết định tìm kiếm; NoEQ chiếu thành phần equity về 0, bỏ kênh thuần Gini và
giữ hệ số distance/time kế thừa. Chính sách được áp dụng ở archive, SA,
trọng số, reward và perturbation liên quan equity. Cả hai vẫn tính và xuất
Gini thật ở `Solution`/kết quả cuối. Không sửa Gini báo cáo về 0 để làm NoEQ.

CMake cố định ALNS_NO_EQUITY theo nhánh; `configs/variant.json` giúp Python
phát hiện executable sai biến thể. Không có khóa JSON chọn Full/NoEQ trong
cùng một nhánh.

## 7. Log, kiểm chứng và tái lập

Mỗi solver chạy trong process riêng với seed riêng. ThreadPoolExecutor chỉ
điều phối subprocess; parent là nơi ghi summary. Input được snapshot; metadata
lưu SHA256 input/binary và tham số đã resolve. Không dùng các driver legacy
để suy ra tham số của `run.py`.

Lịch trình được xuất với độ chính xác đầy đủ trước khi checker kiểm định;
checker không chữa lời giải. C++ và Python kiểm định độc lập; Python đọc lại
raw schedule và geometry từ input. Một lỗi ở bất kỳ lớp nào làm launcher
đánh dấu thất bại. Log chi tiết mỗi vòng là vòng ALNS chính, không có nghĩa
mỗi ứng viên move nội bộ của LS đều có một bản lịch trình riêng trên đĩa.

Để thêm tham số mới: khai báo/default ở ALNSConfig, cập nhật defaults và kiểm
tra kiểu/miền tại Python, ánh xạ ở ExperimentRunner, nối tới điểm thực sự dùng,
cập nhật tài liệu và kiểm tra chức năng. Để đổi thuật toán, kiểm tra git trước,
giữ baseline, chạy test trước/sau; thay đổi tính khả thi cần kiểm tra replay
trên dữ liệu thật ngoài fixture nhỏ.

Mặc định CMake chỉ build entry point thí nghiệm và library. Test C++ lịch sử
có thể bật `-DBUILD_TESTING=ON`; công cụ debug/tuning cũ bật bằng
`-DALNS_BUILD_LEGACY_TOOLS=ON`. Không cần bật chúng để sử dụng giao diện trên.
