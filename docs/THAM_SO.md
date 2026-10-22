# Tham số thí nghiệm

Nguồn giá trị chuẩn là [`configs/default.json`](../configs/default.json).
JSON là object **phẳng**: khóa có dấu chấm bên dưới vẫn là một chuỗi khóa,
không phải object lồng nhau. File riêng có thể chỉ ghi các khóa cần override.
Cả hai nhánh kế thừa cùng giá trị số học; variant do nhánh quyết định.

## Điều khiển chạy, không đặt trong JSON

| Tùy chọn CLI | Mặc định / yêu cầu | Tác dụng |
|---|---|---|
| `--instance` | Bắt buộc ở single; chọn thay cho directory ở benchmark | Một file instance |
| `--instances-dir` | Chỉ benchmark | Mọi file khớp pattern ngay trong thư mục |
| `--pattern` | `*.txt` | Lọc tên file trong directory, không chứa dấu phân cách đường dẫn |
| `--config` | `configs/default.json` | JSON override tham số thuật toán |
| `--binary` | `build-release/evrptw` hoặc `.exe` | Executable đúng variant của checkout |
| `--output` | Bắt buộc, chưa tồn tại | Nơi lưu kết quả |
| `--seed` | 1, chỉ single | Seed trong [1, 4294967295] |
| `--workers` | Bắt buộc ở benchmark, > 0 | Số solver process đồng thời tối đa |
| `--runs` | 10, chỉ benchmark, > 0 | Số lần chạy mỗi instance |
| `--seed-start` | 1, chỉ benchmark | Seed đầu; chạy liên tiếp runs seed, không tràn uint32 |

## ALNS và dừng tìm kiếm

| Khóa JSON | Mặc định | Ý nghĩa / miền giá trị |
|---|---:|---|
| `maxIterations` | 25000 | Số vòng ALNS tối đa, nguyên > 0 |
| `maxTime` | 1821000 | Ngân sách tìm kiếm ALNS, mili giây, nguyên > 0; giới hạn mềm |
| `segmentIterations` | 200 | Số vòng mỗi segment cập nhật trọng số, nguyên > 0 |
| `decayParameter` | 0.85 | Hệ số giữ trọng số cũ, [0,1]; w mới = decay × w cũ + (1-decay) × điểm trung bình, có floor 0.1 |
| `scoreDominating` | 40 | Điểm thưởng kênh ứng viên trội, số >= 0 |
| `scoreNonDominated` | 20 | Điểm thưởng kênh không bị trội, >= 0 |
| `scoreDominated` | 10 | Điểm kênh bị trội được áp dụng theo logic chấp nhận, >= 0 |
| `scoreIdentical` | 15 | Điểm kênh tương đương, >= 0 |
| `minRemoval` | 0.15 | Tỷ lệ xóa cơ sở nhỏ nhất, [0,1] |
| `maxRemoval` | 0.55 | Tỷ lệ xóa cơ sở lớn nhất, minRemoval <= maxRemoval <= 1 |
| `hvImprovementThreshold` | 0.001 | Ngưỡng cải thiện HV dùng theo dõi hội tụ, >= 0 |
| `hvStagnationLimit` | 20 | Giới hạn đếm trì trệ HV, nguyên > 0; không phải số vòng liên tiếp không cải thiện |

Mức xóa thực tế còn chịu perturbationBoost và giới hạn số khách trong solver.
Điểm thưởng có thể được bổ sung bởi các nhánh giảm xe/cải thiện mục tiêu; các
khóa score không mô tả toàn bộ chính sách thưởng. Xem `ALNSSolver.cpp`.

## Simulated annealing

| Khóa | Mặc định | Ý nghĩa / miền |
|---|---:|---|
| `startTemperature` | 200 | Nhiệt độ khởi đầu, > 0 |
| `coolingRate` | 0.9993 | Hệ số làm nguội, (0,1] |
| `minTemperature` | 0.05 | Nhiệt độ sàn, > 0 và <= startTemperature |
| `SAScale` | 50 | Hệ số scale trong đánh giá SA, > 0 |

NoEQ bỏ thành phần Gini khỏi quyết định SA, vẫn giữ các giá trị chung này.

## Local search và tối ưu thực thi

| Khóa | Mặc định | Ý nghĩa / miền |
|---|---:|---|
| `useLocalSearch` | true | Bật LS trong quá trình tìm kiếm |
| `localSearchIntensity` | 15 | Cường độ xác suất cơ sở, nguyên [0,100]; không phải số bước LS |
| `reconstructionCacheMode` | 2 | 0 tắt, 1 observe, 2 sử dụng cache |
| `useSelectiveEjectionUndo` | true | Hoàn tác chọn lọc khi thử ejection |
| `orOptCacheMode` | 0 | 0 tắt, 1 observe, 2 sử dụng cache block OrOpt |
| `useChargingRepairLS` | false | Cho phép LS thử sửa tuyến bằng charging repair |
| `chargingRepairTrialsPerLS` | 8 | Số lượt thử charging repair mỗi lần gọi LS, nguyên >= 0 |
| `chargingRepairBudget.maxEvaluations` | 80 | Ngân sách số đánh giá mỗi repair, nguyên >= 0 |
| `chargingRepairBudget.maxStations` | 6 | Giới hạn số trạm trong repair, nguyên >= 0 |
| `chargingRepairBudget.stationCandidates` | 3 | Số trạm ứng viên xét, nguyên >= 0 |
| `chargingRepairBudget.beamWidth` | 8 | Độ rộng beam, nguyên >= 0 |
| `chargingRepairBudget.maxDurationMs` | 3 | Ngân sách thời gian repair, mili giây, nguyên >= 0 |

Ở vòng ALNS chính, xác suất gọi LS được điều chỉnh bởi
`ALNSConfig::localSearchProbability(iteration,numRoutes)`: giai đoạn <= 2000
vòng dùng scale 1.5; sau đó trước 25% số vòng dùng 1.2; các điều kiện ít tuyến
và cuối quá trình có thể thay scale xuống 0.85/0.5/0.45. Kết quả lấy phần nguyên
và chặn [0,100]. Vì vậy intensity 15 không có nghĩa mọi vòng đều dùng đúng 15%.
Hàm tìm kiếm phụ có thể dùng trực tiếp cường độ cơ sở.

Observe phục vụ đo/đối chiếu, không bật đường tắt dùng kết quả cache. Muốn tắt
charging repair dùng boolean tương ứng; không dùng ngân sách 0 để thay cho cờ
tắt vì từng giới hạn được xử lý tại module repair. Cờ charging không tắt cơ chế
khôi phục tính khả thi thông thường của Route/repair operators.

## Scatter Search tùy chọn

| Khóa | Mặc định | Ý nghĩa |
|---|---:|---|
| `useScatterSearch` | false | Bật pha Scatter Search tùy chọn |
| `scatterSearchConfig.refSetSizeQuality` | 5 | Kích thước nhóm chất lượng |
| `scatterSearchConfig.refSetSizeDiversity` | 5 | Kích thước nhóm đa dạng |
| `scatterSearchConfig.alnsItersPerCombination` | 30 | Số vòng ALNS cho mỗi tổ hợp |
| `scatterSearchConfig.maxScatterIters` | 5 | Số vòng Scatter tối đa |
| `scatterSearchConfig.timeLimitMs` | 30000 | Ngân sách Scatter, mili giây |
| `scatterSearchConfig.stagnationThreshold` | 2 | Ngưỡng trì trệ của Scatter |
| `scatterSearchConfig.maxPRMoves` | 25 | Số bước path relinking tối đa |

Các trường số của Scatter là nguyên >= 0. Chúng chỉ có tác dụng khi pha này
được gọi; thay cấu hình tùy chọn là thay thiết kế thí nghiệm, nên lưu file JSON
riêng để đối chiếu.

## Kiểu dữ liệu và khả năng tái lập

Boolean phải là JSON true/false; số phải hữu hạn và không âm. Các trường nguyên
không nhận số thập phân: maxTime tối đa int64, các trường nguyên khác tối đa
int32. Khóa không có trong default bị từ chối. Không tự thêm trường của
`ALNSConfig` chưa được giao diện ánh xạ, chẳng hạn regretK: toán tử hiện đăng ký
RegretKRepair với K=3 trong C++.

Giao diện luôn bật logging, profile LS và log mỗi vòng ALNS chính; không có
tham số JSON tắt các mục này. Chi phí ghi log nằm trong thời gian đo. Giữ nguyên
input, branch/commit, binary, config và seed khi so sánh. Giới hạn theo thời
gian, chạy đồng thời và các module có budget thời gian có thể làm số vòng/kết
quả khác giữa các máy; seed không bảo đảm toàn bộ file có timestamp/timing
giống từng byte.

Xem `parameters.json`, `run.json`, `run_statistics.json` của mỗi seed để biết
tham số thực dùng, binary/input hash và lý do dừng; file config bên ngoài có
thể đã bị chỉnh sau khi chạy.
