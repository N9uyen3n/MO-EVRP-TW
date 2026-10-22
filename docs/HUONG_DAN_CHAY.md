# Hướng dẫn chạy EVRPTW-PR

Hai nhánh dùng chung giao diện: `first` là FP-PALNS, `first-noEq` là
FP-PALNS-NoEQ. Cả hai báo cáo Z1, Z2, Z3, Z4; NoEQ bỏ Gini khỏi quyết định
tìm kiếm nhưng vẫn tính Gini thật khi xuất kết quả. Tham số số học kế thừa
giống nhau, không hiệu chỉnh riêng NoEQ.

## 1. Chuẩn bị và biên dịch

Cần CMake >= 3.17, trình biên dịch C++17, Python >= 3.10. Không cần cài thư viện
Python ngoài. Các lệnh dưới chạy tại thư mục gốc repository.

Trong WSL Ubuntu, mở repository rồi chạy:

```sh
cd /mnt/d/Work/SLSCM-LaB/24092025/untitled
git switch first
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --target evrptw -j4
python3 run.py single --help
python3 run.py benchmark --help
```

Chọn `git switch first-noEq` nếu muốn chạy NoEQ. Sau khi chuyển nhánh phải
configure/build lại; nên dùng checkout và thư mục build riêng cho mỗi nhánh.
Launcher kiểm tra biến thể của binary, từ chối dùng binary khác nhánh.
Không build lại binary trong lúc benchmark đang chạy.

Trên Windows có compiler/CMake/Python sẵn, dùng các lệnh CMake tương tự.
Với Visual Studio, thêm `--config Release` vào lệnh build; khi chạy thêm
`--binary build-release/Release/evrptw.exe`. Trên WSL dùng `python3`, trên Windows
có thể dùng `python`. Các ví dụ sau dùng `python3`.

`-j4` là số tác vụ **biên dịch**, độc lập với `--workers` khi chạy benchmark.

## 2. Dữ liệu hỗ trợ

Giao diện này nhận file Solomon EVRPTW hiện có, ví dụ
`data/solomon/r101_21.txt`: depot `d`, trạm `f`, khách `c`, tọa độ, nhu cầu,
cửa sổ thời gian, thời gian phục vụ, và các dòng `Q`, `C`, `r`, `g`, `v`.
Q là dung lượng pin, C là tải trọng, r là tiêu hao năng lượng/độ dài,
g là thời gian/năng lượng sạc, v là vận tốc. Tọa độ tạo khoảng cách Euclid
đối xứng, thời gian di chuyển bằng khoảng cách/v.

Bài toán chỉ giao hàng, không nhận hàng; được sạc một phần tại trạm.
Chưa hỗ trợ JD hay file Solomon VRPTW thiếu tham số xe điện trong giao diện này.
Đường dẫn chứa khoảng trắng phải đặt trong dấu nháy.

## 3. Chạy một instance, một seed

```sh
python3 run.py single --instance data/solomon/r101_21.txt --config configs/default.json --seed 1 --output results/single_r101/seed_1
```

`--instance`: đúng một file; `--seed`: số nguyên 1..4294967295, mặc định 1;
`--config`: JSON tham số, mặc định `configs/default.json`;
`--output`: thư mục kết quả của chính lần chạy đó, bắt buộc chưa tồn tại.
`--binary`: tùy chọn nếu executable không ở `build-release/evrptw`.
Lệnh single không tự tạo thêm tầng tên instance/seed ngoài `--output`.

Kiểm định nhanh giao diện bằng `--config configs/smoke.json`: 20 vòng, tắt LS.
Cấu hình này phục vụ kiểm tra hoạt động, không dùng đánh giá chất lượng thuật toán.

## 4. Benchmark một file, lặp nhiều lần

Mặc định chạy 10 lần, seed 1 đến 10:

```sh
python3 run.py benchmark --instance data/solomon/r101_21.txt --config configs/default.json --workers 4 --output results/r101_10runs
```

Ví dụ chạy 3 lần, seed 3, 4, 5:

```sh
python3 run.py benchmark --instance data/solomon/r101_21.txt --config configs/default.json --workers 2 --runs 3 --seed-start 3 --output results/r101_seeds3to5
```

`--runs N` là số lần chạy **mỗi instance**, phải > 0. `--seed-start S` là seed
đầu tiên; seed cuối bằng S + N - 1, không vượt 4294967295.

## 5. Benchmark cả thư mục

```sh
python3 run.py benchmark --instances-dir data/solomon --config configs/default.json --workers 4 --output results/solomon_10runs
```

Chỉ chọn file `*.txt` ngay trong thư mục, không tìm đệ quy. Có thể lọc tên:

```sh
python3 run.py benchmark --instances-dir data/solomon --pattern 'rc*.txt' --config configs/default.json --workers 4 --runs 10 --seed-start 1 --output results/rc_10runs
```

Dùng đúng một trong `--instance` hoặc `--instances-dir`. `--pattern` chỉ có tác
dụng với thư mục. Tổng số job = số file được chọn × `--runs`.
Ví dụ 12 file × 10 seed = 120 job; `--workers 4` cho tối đa 4 solver chạy đồng
thời. Đây là các **tiến trình độc lập**, mỗi tiến trình có RNG/seed và log riêng,
không phải bốn luồng hợp tác trong cùng một lần tìm kiếm. Chọn workers theo số
CPU và RAM; chạy nhiều tiến trình có thể thay đổi thời gian thực đo được.

```text
results/solomon_10runs/
  benchmark.json
  summary.csv
  r101_21/
    seed_1/
    seed_2/
    ...
    seed_10/
  r104_21/
    seed_1/
    ...
```

Không ghi đè hoặc tự resume thư mục cũ; chọn output mới cho mỗi đợt.
Ctrl+C yêu cầu dừng các solver đang chạy, giữ kết quả/log đã ghi và đánh dấu
benchmark bị hủy. Job chưa hoàn tất không được coi là kết quả hợp lệ.

## 6. Setup tham số

Copy `configs/default.json` thành file riêng hoặc tạo JSON chỉ chứa các giá trị
cần thay. Ví dụ `configs/my_run.json`:

```json
{
  "maxIterations": 10000,
  "maxTime": 600000,
  "useLocalSearch": true,
  "localSearchIntensity": 15
}
```

Sau đó dùng `--config configs/my_run.json` cho single hoặc benchmark.
Những khóa không ghi được lấy từ `configs/default.json` của checkout hiện tại;
`parameters.json` trong mỗi seed lưu toàn bộ giá trị thực sự đã dùng.
Không ghi seed/workers/runs vào JSON thuật toán: chúng là tùy chọn dòng lệnh.
JSON dùng `true`/`false`, không dùng 1/0 cho boolean; không có comment/trailing
comma. Khóa lạ, trùng khóa, sai kiểu hoặc ngoài miền cho phép bị từ chối.

Xem [toàn bộ tham số và đơn vị](THAM_SO.md). `maxTime` tính bằng **mili giây**,
là giới hạn mềm phần tìm kiếm ALNS; không phải hard timeout của tiến trình.
Khởi tạo, xuất log, kiểm định và Scatter Search tùy chọn có thời gian riêng;
một neighborhood đang thực hiện có thể vượt hạn. HV stagnation hoặc số vòng
cũng có thể làm solver dừng trước maxTime. Đọc lý do thực tế trong
`run_statistics.json` và `summary.csv`.

## 7. Đọc kết quả và kiểm định

`summary.csv` cấp benchmark cập nhật sau mỗi job, sắp theo tên instance/seed.
Các cột gồm status, vehicles, distance, gini, max_time, solution, front_size,
routes, invalid, iterations, stop_reason, solver_ms, wall_ms và đường dẫn output.
`benchmark.json` lưu cấu hình, số workers/job, miền seed và tiến độ.

Mỗi seed chứa toàn bộ archive trong `results.csv`; một dòng là một lời giải.
`representative.json` chọn lời giải ít xe nhất rồi quãng đường nhỏ nhất để
tóm tắt; không có nghĩa toàn bộ Pareto front chỉ có lời giải này.

| File trong mỗi seed | Ý nghĩa |
|---|---|
| `input.txt` | Bản chụp input dùng trong lần chạy |
| `parameters.json`, `parameters.cfg` | Bộ tham số đầy đủ và bản truyền cho C++ |
| `run.json`, `solver_metadata.json` | Variant, seed, lệnh, hash input/binary, thời điểm, trạng thái |
| `stdout.log`, `stderr.log`, `runner_error.txt` | Console/error; runner_error chỉ xuất hiện khi lỗi |
| `results.csv` | solution, vehicles (Z1), distance (Z2), gini (Z3), max_time (Z4), valid của C++ |
| `schedules.csv` | Lịch trình thật của mọi lời giải: route, position, node, arrival, departure, wait, battery, load, charge |
| `cpp_validation.txt` | Lỗi replay C++; rỗng nghĩa là không phát hiện lỗi |
| `validation.json`, `validation.txt` | Kết luận và vi phạm từ checker Python độc lập |
| `representative.json` | Lời giải đại diện và quy tắc chọn |
| `search_iterations.csv`, `search_evolution_log.txt` | Mỗi vòng ALNS chính: toán tử, kết quả chấp nhận, mục tiêu và ứng viên chi tiết |
| `search_progress.csv`, `search_operators.csv` | Tiến triển archive, trọng số/điểm/số lần dùng toán tử theo segment |
| `search_config.txt`, `search_summary.txt`, `search_front_objectives.csv`, `search_front_details.txt` | Cấu hình và front cuối |
| `run_statistics.json`, `local_search_profile.json` | Lý do dừng, thời gian và thống kê LS/charging |
| `insertion_time_bounds.json`, `reconstruction_cache.json`, `ejection_undo.json`, `or_opt_cache.json` | Chẩn đoán tối ưu hiệu năng; giá trị 0 có thể do tính năng tắt |

Kiểm tra lại một seed đã lưu, không cần chạy thuật toán:

```sh
python3 tools/validate.py --run results/solomon_10runs/r101_21/seed_1
```

Checker kiểm tra hash input và replay lịch trình **đã xuất**, không sửa tuyến,
không tự chọn lượng sạc hoặc xếp lịch lại. Kiểm tra phục vụ mỗi khách đúng một
lần, depot đầu/cuối, tải giao hàng, pin trước/sau sạc, thời gian sạc/phục vụ/chờ,
cửa sổ thời gian và cả bốn mục tiêu. `validation.json` phải có `valid: true`.
Launcher chỉ đánh dấu `status: ok` khi solver kết thúc thành công và Python
checker chấp nhận mọi lời giải đã trả về. Tính khả thi không chứng minh tối ưu.

Exit code launcher: 0 nếu mọi job thành công, 1 nếu lỗi cấu hình/chạy/kiểm định,
130 nếu hủy bằng Ctrl+C. Checker riêng: 0 hợp lệ, 3 không hợp lệ.

Nếu thất bại, đọc `runner_error.txt`, `stderr.log`, `validation.txt`, `run.json`.
Lỗi khác biến thể binary: build lại đúng nhánh. Lỗi output tồn tại: đổi đường
dẫn. Không lấy dòng summary bị `failed` làm kết quả thực nghiệm hợp lệ.

## 8. Kiểm tra nhanh sau thay đổi code

```sh
python3 -m tools.test_experiments --binary build-release/evrptw
```

Chạy từ đúng checkout của variant. Bộ kiểm tra dùng instance nhỏ, kiểm tra
single, benchmark nhiều seed, tính độc lập tiến trình, file log và checker phát
hiện lịch trình bị sửa. Đây là kiểm tra chức năng nhanh, không phải benchmark
chất lượng. Xem [bản đồ mã nguồn](SOURCE_CODE.md) trước khi sửa thuật toán.
