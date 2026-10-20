# Kiểm chứng EVRPTW-PR

Phạm vi thí nghiệm hiện tại: chỉ giao hàng, sạc một phần, dữ liệu Solomon với khoảng cách Euclidean đối xứng. Các fixture mới của charging repair theo đúng mô hình này.

## Regression C++

```powershell
wsl -d Ubuntu-22.04 --exec cmake -S . -B build-audit-wsl-p34-after -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
wsl -d Ubuntu-22.04 --exec cmake --build build-audit-wsl-p34-after --target ALNSAuditRunner RouteRegressionTests LocalSearchRegressionTests ChargingRepairTests -j 6
wsl -d Ubuntu-22.04 --exec ctest --test-dir build-audit-wsl-p34-after --output-on-failure
```

Các kiểm tra dùng exception thay vì assert nên vẫn hoạt động trong Release/NDEBUG.

## Chạy solver và replay lịch chính xác

CLI của ALNSAuditRunner:

```text
ALNSAuditRunner input output seconds seed [maxIterations=25000] [chargingRepair=0|1] [profileLS=0|1]
```

Runner ghi `results.csv`, `schedules.csv`, `validation.txt`, `run_statistics.json` và `audit_parameters.json`; từ chối ghi đè thư mục output. Charging repair mặc định tắt. Ngân sách thời gian là giới hạn mềm của vòng ALNS, khởi tạo và một LS đang chạy có thể khiến wall time lớn hơn ngân sách.

Kiểm tra độc lập sau khi run hoàn tất (Python stdlib, không phụ thuộc code evaluator):

```powershell
python src/test/validate_audit_schedule.py data/test-100/r104_21.txt path/to/output path/to/independent_validation.json
```

Script replay trực tiếp trạng thái CSV đủ độ chính xác: di chuyển, pin trước/sau sạc, tải giao hàng, TW bắt đầu phục vụ, giờ hoàn tất sạc, coverage, Distance/Gini/MaxTime. Không sửa lịch hoặc thay lượng sạc. Exit 0 khi mọi nghiệm đạt, 3 khi có nghiệm lỗi/không có nghiệm; report output được mở bằng chế độ không ghi đè.

## Đối chiếu bật/tắt P3–P4

`run_charging_repair_audit.py` chạy trong WSL/Linux, dùng cùng một executable đã copy và đóng băng cho cả hai mode. Có preflight so mode off với executable baseline đã giữ, bằng seed và số vòng cố định, trước khi chạy 18 run tuần tự trên r104/r101/c109, seed 1/2/3, 60 giây/run. Mỗi run đều được replay độc lập. Không chạy build hoặc benchmark khác đồng thời.

```text
python3 src/test/run_charging_repair_audit.py build-audit-wsl-p34-after/ALNSAuditRunner artifacts/alns_p3_p4/before/ALNSAuditRunner artifacts/alns_p3_p4/new_paired_run
```

Lưu ý: 60 giây là ngân sách tối đa; điều kiện HV/số vòng vẫn có thể dừng sớm. So sánh bằng số xe trước, rồi các mục tiêu khi cùng số xe. Ghi cả kết quả tốt hơn và kém hơn; một vài seed không đủ chứng minh ưu thế tổng quát.

## Kiểm tra log cũ làm tròn hai chữ số

```powershell
python -m unittest discover -s src/test -p test_validate_legacy_front.py -v
python src/test/validate_legacy_front.py data/solomon/r103_21.txt logs_testLS5/r103_21 update/new_legacy_audit
```

Phần này cần NumPy/SciPy. Nó phân biệt ba câu hỏi: lịch log có nhất quán sau xét làm tròn; chỉ sửa trạng thái pin có đủ không; giữ nguyên thứ tự khách/trạm nhưng lập lại lịch sạc có khả thi không. LP chỉ dùng để kiểm chứng/lập lịch lại log cũ, không nằm trong thuật toán ALNS hoặc benchmark charging repair.

Gini của lịch LP tái lập là một giá trị khả thi, không được coi là Gini tối ưu hoặc mặc nhiên thay thế chỉ số của lần chạy cũ. Xem `update/audit_r103_21_legacy_report.md`.

## Profiling local search trước khi chọn cache P5

`profileLS=1` bật đo bằng `steady_clock`; mặc định tắt. Runner ghi thêm `local_search_profile.json` ở cả hai mode. Mỗi hàm có `calls`, `inclusive_ns` (bao gồm các hàm con được đo), `self_ns` (đã trừ các hàm con) và `vnd_improvements` (số lượt tìm kiếm VND trả về cải thiện, chỉ áp dụng cho tám toán tử VND). Không cộng thời gian inclusive của hàm cha với hàm con. Đây là wall time, không phải CPU time; số lần `evaluate_move` không bao gồm những ứng viên được helper khác đánh giá trực tiếp.

Đợt đo độc lập dùng r104/r101/c109, seed 1/2, 200 vòng cố định, charging repair tắt, giới hạn thời gian dự phòng 3.600 giây. Cấu hình tổng số vòng cũng ảnh hưởng các ngưỡng thích ứng; đây không phải 200 vòng đầu của cấu hình 25.000 vòng. Mỗi bộ chạy bản trước sửa, bản mới tắt và bản mới bật profiling; đối chiếu lịch từng byte và mục tiêu, rồi replay vật lý độc lập. Chạy tuần tự, tránh build hay benchmark khác đồng thời.

```text
mkdir -p artifacts/alns_p5/new_profile
cp path/to/preserved/ALNSAuditRunner artifacts/alns_p5/new_profile/ALNSAuditRunner_before
python3 src/test/run_ls_profile_audit.py artifacts/alns_p5/new_profile/ALNSAuditRunner_before build-audit-wsl-p5-profile/ALNSAuditRunner artifacts/alns_p5/new_profile 200
python3 src/test/summarize_ls_profile_audit.py artifacts/alns_p5/new_profile/experiment update/new_profile_results.json
```

Runner lưu bản copy executable, nguồn, input, SHA-256, Git HEAD, cấu hình, log và kết quả; từ chối ghi đè thư mục thí nghiệm. Script yêu cầu đủ vòng và dừng bởi `iteration_limit`, không chấp nhận run bị cắt do thời gian. Đo overhead bật/tắt cùng executable; so bản trước sửa để phát hiện ảnh hưởng của instrumentation khi tắt. Hai seed chỉ hỗ trợ xác định chỗ cần khảo sát tiếp, chưa đủ kết luận tốc độ tổng quát hoặc chất lượng thuật toán.
