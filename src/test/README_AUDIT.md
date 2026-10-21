# Kiểm chứng EVRPTW-PR

Phạm vi thí nghiệm hiện tại: chỉ giao hàng, sạc một phần, dữ liệu Solomon với khoảng cách Euclidean đối xứng. Các fixture mới của charging repair theo đúng mô hình này.

## Regression C++

```powershell
wsl -d Ubuntu-22.04 --exec cmake -S . -B build-audit-wsl-p34-after -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
wsl -d Ubuntu-22.04 --exec cmake --build build-audit-wsl-p34-after --target ALNSAuditRunner RouteRegressionTests LocalSearchRegressionTests ChargingRepairTests ReconstructionCacheTests EjectionUndoTests -j 6
wsl -d Ubuntu-22.04 --exec ctest --test-dir build-audit-wsl-p34-after --output-on-failure
```

Các kiểm tra dùng exception thay vì assert nên vẫn hoạt động trong Release/NDEBUG.

## Chạy solver và replay lịch chính xác

CLI của ALNSAuditRunner:

```text
ALNSAuditRunner input output seconds seed [maxIterations=25000] [chargingRepair=0|1] [profileLS=0|1] [reconstructionCache=0|1|2] [selectiveEjection=0|1]
```

Runner ghi `results.csv`, `schedules.csv`, `validation.txt`, `run_statistics.json` và `audit_parameters.json`; từ chối ghi đè thư mục output. Charging repair và profiling mặc định tắt. Cache tái dựng dùng mode 0=tắt, 1=chỉ quan sát, 2=bật; `ALNSConfig` mặc định bật mode 2 sau kiểm chứng P5. Ngân sách thời gian là giới hạn mềm của vòng ALNS, khởi tạo và một LS đang chạy có thể khiến wall time lớn hơn ngân sách.

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

## Cache tái dựng tuyến P5

`reconstructionCache=0|1|2` lần lượt là tắt, quan sát và bật cache thất bại của helper xác định. `reconstruction_cache.json` ghi số yêu cầu, số phép tính thật, thành công/thất bại đã tính, đầu vào thất bại trùng, hit, insert, session và mức đầy bảng. Mode quan sát vẫn tính mọi yêu cầu; dùng để xác nhận cơ hội cache. Mode bật chỉ trả lại thất bại đã gặp với cùng đầu vào trong một entry point LS. Không giảm số ứng viên hoặc độ sâu ejection.

Khóa dùng full sequence, thứ tự khách thêm, thông số xe và giới hạn trạm, kiểm tra bằng nội dung đầy đủ sau hash. Cache tối đa 4.096 mục và 512 node/khóa, hết hiệu lực sau mỗi `run` hoặc intensified reduction; dữ liệu instance phải cố định trong lời gọi. Helper ngoài session đi đường cũ. Cấu hình solver có `reconstructionCacheMode`; để quay baseline tính toán cũ, đặt `alns::ReconstructionCacheMode::Off`.

```text
mkdir -p artifacts/alns_p5/new_cache
cp path/to/preserved/ALNSAuditRunner artifacts/alns_p5/new_cache/ALNSAuditRunner_before
python3 src/test/run_reconstruction_cache_audit.py artifacts/alns_p5/new_cache/ALNSAuditRunner_before build-audit-wsl-p5-cache/ALNSAuditRunner artifacts/alns_p5/new_cache 200
python3 src/test/summarize_reconstruction_cache_audit.py artifacts/alns_p5/new_cache/experiment update/new_cache_results.json
```

Đợt chuẩn có 21 run: ba run quan sát có profiling trên seed 1, rồi sáu bộ bản trước sửa/bản mới tắt cache/bản mới bật cache trên r104/r101/c109, seed 1/2. Các run so tốc độ tắt profiling; mọi run cùng ngân sách 200 vòng và được replay độc lập. Các bộ phải cho cùng mục tiêu và cùng lịch từng byte; số đầu vào thất bại lặp ở mode quan sát phải khớp số hit ở mode bật. Nguồn và executable được đóng băng, hash được kiểm tra cuối đợt. Không build trong lúc đo.

## Ejection: undo các tuyến đã sửa

Tham số cuối `selectiveEjection=0|1` chọn full snapshot hoặc undo từng tuyến trong Stage B. `ALNSConfig::useSelectiveEjectionUndo` cấu hình mode cho solver. Journal lưu cả Route, giữ checkpoint lồng nhau để commit con vẫn có thể rollback từ cha; giữ nhiều nhất sáu Route với độ sâu tối đa 5. Thứ tự tìm kiếm, giới hạn 2.000 attempts/victim và các bản sao ở Stage A/thử trạm giữ nguyên.

`ejection_undo.json` ghi số checkpoint, bản sao tuyến đã lưu/khôi phục, commit/rollback, peak số tuyến undo, số lời gọi `place` và số nhánh thử. Khi profiling bật, `ejection_snapshot` và `ejection_restore` đo riêng thao tác Stage B; đã nằm trong inclusive của ejection. Mode full mới vẫn sao chép cả vector khi lưu, nhưng khôi phục bằng move; vì vậy script còn chạy executable cũ có bước restore bằng copy để so tác động tổng thể.

```text
mkdir -p artifacts/alns_p5/new_ejection
cp path/to/preserved/ALNSAuditRunner artifacts/alns_p5/new_ejection/ALNSAuditRunner_before
python3 src/test/run_ejection_undo_audit.py artifacts/alns_p5/new_ejection/ALNSAuditRunner_before build-audit-wsl-ejection-undo/ALNSAuditRunner artifacts/alns_p5/new_ejection 200
python3 src/test/summarize_ejection_undo_audit.py artifacts/alns_p5/new_ejection/experiment update/new_ejection_results.json
```

Đợt chuẩn có 24 run: r104/r101/c109, seed 1/2, reconstruction cache bật mode 2, charging repair tắt. Hai run profiling mỗi instance ở seed 1 đo full/selective; sáu bộ trước sửa/full/selective tắt profiling đo tốc độ. Mọi run phải đủ vòng, giữ nguyên mục tiêu và lịch từng byte; các mode mới còn phải giữ nguyên số checkpoint, commit/rollback, `place` và nhánh thử. Khi thời gian có nhiễu lớn, cần đo lặp bằng cùng executable đóng băng trước khi chốt lợi ích.

`repeat_ejection_undo_audit.py initial_experiment new_output` đo lại r104 seed 2 hai lần/mode với thứ tự đảo, ghi wall time và CPU time Linux `getrusage`. Dùng executable/input đã đóng băng của đợt đầu; đối chiếu lịch, mục tiêu và thống kê nhánh với đợt gốc. Thư mục mới phải chưa tồn tại. CPU time trong WSL cũng cần diễn giải theo môi trường, không tự động chứng minh wall time đã cải thiện ổn định.

## A/B tổng: trước cache + undo so với bản cuối

`run_total_speed_audit.py` so trực tiếp revision 226f943 với edc8e92, dùng hai executable đã đóng băng và hồ sơ source/hash từ các đợt trước. Nó đối chiếu source production/runner với Git, lưu ZIP nguồn cùng executable và input; không mở thêm cây source. Công cụ yêu cầu các thư mục hồ sơ `profiling_20261020/experiment` và `ejection_undo_20261020/experiment` còn tồn tại để xác nhận provenance.

```text
python3 src/test/run_total_speed_audit.py artifacts/alns_p5/cache_20261020/ALNSAuditRunner_before artifacts/alns_p5/ejection_undo_20261020/experiment/ALNSAuditRunner_after artifacts/alns_p5/new_total_speed 200 3
python3 src/test/summarize_total_speed_audit.py artifacts/alns_p5/new_total_speed update/new_total_speed_results.json
```

Chuẩn là 36 run đo = 3 instance × 2 seed × 3 lần đo lặp × 2 phiên bản, cộng hai warmup bị loại khỏi bảng thời gian. Các cặp chạy liền nhau, đảo AB/BA, thứ tự instance luân phiên. Ghi wall/CPU time, kiểm chứng từng lịch và so mục tiêu/lịch từng byte qua cả hai phiên bản và mọi lần lặp. Thư mục output phải mới; chỉ tổng hợp khi manifest completed, không loại các cặp chậm hơn. Báo riêng trung vị giảm thời gian theo cặp và tỷ lệ giảm tổng thời gian; không cộng phần trăm của cache và undo từ các thí nghiệm khác.

## P6: giới hạn thời gian trước/sau vị trí chèn

Cờ CMake `ALNS_INSERTION_TIME_BOUNDS=ON|OFF` bật/tắt bộ lọc trong `Route::checkInsertionCost`, mặc định ON sau A/B. Build đã cache OFF phải cấu hình lại với ON; đổi default không ghi đè giá trị trong CMakeCache. Bảng O(n) xây lười theo phiên bản tuyến, chia sẻ bất biến khi copy; mọi thêm/xóa/đảo/clear làm mất hiệu lực. Tra cứu mỗi phép chèn O(1). Thuộc tính bỏ toàn bộ thời gian sạc để tạo lower bound của prefix và upper bound của suffix; ứng viên không bị loại vẫn qua evaluator đầy đủ. Đây không phải chứng nhận khả thi năng lượng. Dữ liệu instance phải bất biến trong lúc dùng Route, cùng hợp đồng với cache evaluator hiện tại.

```text
cmake -S . -B build-audit-wsl-p6-bounds -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DALNS_INSERTION_TIME_BOUNDS=ON
cmake --build build-audit-wsl-p6-bounds --target ALNSAuditRunner InsertionTimeBoundsTests -j 4
python3 src/test/run_insertion_bounds_audit.py artifacts/alns_p6/insertion_bounds_20261021/ALNSAuditRunner_before build-audit-wsl-p6-bounds/ALNSAuditRunner artifacts/alns_p6/insertion_bounds_20261021/experiment 200 3
python3 src/test/summarize_insertion_bounds_audit.py artifacts/alns_p6/insertion_bounds_20261021/experiment update/alns_p6_insertion_bounds_results.json
```

Baseline là P5 đã kiểm chứng, đối chiếu SHA/source với hồ sơ cũ. Thiết kế 18 cặp / 36 run chính cộng hai warmup; không build khi đo. Yêu cầu lịch/mục tiêu từng byte và counters reconstruction/ejection giống nhau, ngoài việc replay độc lập từng nghiệm. File `insertion_time_bounds.json` ghi enabled, requests, builds và rejections; counter tổng hợp tính cả lần lặp đo. Bản tắt không chứa cache tuyến hoặc nhánh lọc trong đường chạy chèn. Không cộng phần trăm P6 với mức tăng tốc cũ.

## P7: thêm rc106/rc205 theo phạm vi người dùng chốt

```text
python3 src/test/run_p7_rc_audit.py artifacts/alns_p7/new_rc_audit --seconds 60 --preflight-iterations 200
python3 src/test/summarize_p7_rc_audit.py artifacts/alns_p7/new_rc_audit update/new_p7_rc_results.json
```

Script dùng hai binary đóng băng đã chứng nhận: baseline 226f943 trước tối ưu tốc độ và current edd709b sau P6. Yêu cầu hồ sơ nguồn các đợt P5/P6 còn tồn tại và build Release có SHA khớp. Preflight tám run / bốn cặp 200 vòng kiểm tra lịch/mục tiêu byte-identical; sau đó 20 run / 10 cặp, năm seed trên hai instance, 60s pha tìm kiếm sau initialization. Deadline mềm: giữ số đo thực và overshoot. Lịch từng run được kiểm chứng độc lập; chất lượng so fleet trước, rồi nghiệm đại diện theo distance, kèm Gini/MaxTime và HV3D tại fleet chung. Không gộp các run RC 60s với các run R/C 200 vòng thành cùng một protocol. Summary từ chối manifest chưa hoàn tất và không loại seed thua. Đợt này không sửa solver hoặc cấu hình riêng của người dùng.
