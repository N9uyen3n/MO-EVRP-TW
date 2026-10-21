# P7 — Bổ sung rc106 và rc205

Người dùng chốt chạy thêm rc106 và rc205 là đủ phạm vi P7. Đợt này bổ sung hai instance RC vào bằng chứng r104/r101/c109 đã có; không mở rộng pickup/SPD/JD hoặc ma trận có hướng. Không sửa thuật toán cho benchmark.

## Thiết kế chốt trước khi chạy

- Git trước chạy `edd709b`, nhánh `feat/alns-charging-repair-p3-p4`. 60 ca / 6 target CTest Release đạt lại. Giữ nguyên chỉnh sửa người dùng trong `src/test/TestALNS.cpp` (r103, scoreNonDominated 20 lúc kiểm tra).
- Input `data/test-100/rc106_21.txt`, `rc205_21.txt`: mỗi instance 100 khách / 21 trạm / một depot, SHA giống bản ở `data/solomon`.
- rc106 SHA `b33c34c1fdd755eeb864e67703b61f8b99ebc7e1b11d90c4a61cc881db39161e`; rc205 SHA `48d4a95f321146d0a11a95234da0ae0b1b6605c1fecdc8c18bbe4245a8793c4a`.
- Baseline `226f943`: evaluator đã sửa và kiểm chứng, trước cache/undo/P6; executable SHA `59605d7c3fb148b167803b7d76d7819df1d6734085434ce00992014d28f31619`.
- Current `edd709b`: cache tái dựng mode 2, selective undo và P6 time bounds ON; executable SHA `420016ebbd7dda5744da6b5ccee941fd3a770831a1484d1623f0c64709cddd25`.
- Hai bản tắt charging repair/profiling/Scatter; LS intensity 25, scoreNonDominated 25, HV stagnation 20, segment 200. Không dùng cấu hình riêng TestALNS.cpp làm audit.
- Preflight: hai instance × seed 1/2 × hai bản = 8 run, 200 vòng cấu hình, safety 3.600s. Yêu cầu đủ vòng, lịch/mục tiêu từng byte giống nhau. Dùng làm warmup, không gộp với số đo chất lượng.
- Chất lượng: hai instance × năm seed 1–5 × hai bản = 20 run / 10 cặp; ngân sách tìm kiếm 60s, maxIterations 25.000. Đảo AB/BA cân bằng, luân phiên case, chạy tuần tự, không build cạnh tranh, giữ mọi run.
- Ngân sách tính pha tìm kiếm sau khởi tạo, deadline mềm. Báo initialization/search/total wall, vượt search deadline và lý do dừng; không coi mọi run là đúng 60s tổng.
- Replay C++ và Python độc lập trên CSV đầy đủ độ chính xác; không sửa lịch/reschedule/bỏ nghiệm xấu. Lưu ZIP nguồn, flags, executable, input, công cụ và SHA; kiểm tra tính toàn vẹn cuối đợt.

## Cách so sánh

Số xe ưu tiên tuyệt đối. Nghiệm đại diện chọn ít xe rồi distance nhỏ nhất; Gini/MaxTime của chính nghiệm đó ghi kèm, không mặc định cùng cải thiện. HV3D dùng toàn front tại mức xe tốt nhất chung của instance, chuẩn hóa `(fleet × horizon × velocity, 1, horizon)`, reference `(1.1, 1.1, 1.1)`. Bản chưa đạt fleet chung nhận HV null, không coi như thắng/thua HV; số xe đã quyết định ưu tiên.

Hai bản có thể chạy khác số vòng/front dưới giới hạn thời gian; không yêu cầu lịch trùng ở phần chất lượng hoặc suy tỷ lệ tăng tốc từ hai run cùng đặt 60s. Kiểm tra giữ ngữ nghĩa nằm riêng ở preflight cùng số vòng.

## Kết quả đã hoàn tất

Manifest `completed`, đủ **8 preflight + 20 run chất lượng**. Preflight: **58 nghiệm / 446 tuyến**, cả bốn cặp có mục tiêu và lịch từng byte giống nhau. Chất lượng: **258 nghiệm / 2.283 tuyến**. Tổng **316 nghiệm / 2.729 tuyến, 0 vi phạm** qua C++ và Python replay độc lập. Đây là số lần xuất trong audit, có tính các nghiệm/tuyến trùng giữa phiên bản hoặc cấu hình.

| Instance | Fleet baseline → current, cả 5 seed | Trung vị distance đại diện baseline → current | Seed distance tốt hơn / bằng / kém | HV3D tốt hơn / bằng / kém |
|---|---|---|---|---|
| rc106_21 | 13 → 13 | 1.432,682 → 1.420,263 | 2 / 3 / 0 | 5 / 0 / 0 |
| rc205_21 | 4 → 4 | 1.218,765 → 1.211,607 | 2 / 3 / 0 | 4 / 1 / 0 |

Tổng hợp theo từng cặp: **4 thắng / 6 hòa / 0 thua** trên tiêu chí fleet rồi distance; **9 thắng / 1 hòa / 0 thua HV3D**. Không có cải thiện số xe trong đợt này, và không khẳng định số xe trên là tối ưu. Trung vị mức giảm distance theo cặp trên cả 10 cặp là **0%**, vì sáu cặp hòa; các trung vị distance trong bảng là thống kê riêng của năm seed mỗi bản.

| Instance | Seed | Distance baseline | Distance current | Giảm distance | HV3D |
|---|---:|---:|---:|---:|---|
| rc106_21 | 1 | 1.429,399 | 1.429,399 | 0% | tốt hơn |
| rc106_21 | 2 | 1.444,748 | 1.403,905 | 2,8270% | tốt hơn |
| rc106_21 | 3 | 1.432,682 | 1.420,263 | 0,8668% | tốt hơn |
| rc106_21 | 4 | 1.453,832 | 1.453,832 | 0% | tốt hơn |
| rc106_21 | 5 | 1.405,484 | 1.405,484 | 0% | tốt hơn |
| rc205_21 | 1 | 1.218,765 | 1.218,765 | 0% | bằng |
| rc205_21 | 2 | 1.198,054 | 1.198,054 | 0% | tốt hơn |
| rc205_21 | 3 | 1.220,394 | 1.211,607 | 0,7200% | tốt hơn |
| rc205_21 | 4 | 1.207,368 | 1.206,339 | 0,0852% | tốt hơn |
| rc205_21 | 5 | 1.224,747 | 1.224,747 | 0% | tốt hơn |

Không đồng nghĩa mọi mục tiêu đều tốt hơn: nghiệm distance tốt nhất ở rc106 seed 2 và 3 có MaxTime tăng **238,159 → 239,212** (khoảng 1,053), trong khi Distance/Gini giảm. Cả hai vẫn trong deadline depot 240. Các chỉ số chính xác theo seed, gồm Gini/MaxTime và HV3D, nằm trong JSON/CSV. Đây là đánh đổi đa mục tiêu, không phải lỗi khả thi.

HV dùng fleet chung 13 cho rc106, 4 cho rc205; normalization lần lượt `(3120,1,240)` và `(3840,1,960)`, reference `(1.1,1.1,1.1)`. Cả 10 cặp cùng fleet nên không có HV null. Front rộng hơn có thể cải thiện HV ngay khi nghiệm distance đại diện không thay đổi.

## Thời gian và số vòng

Tất cả **20/20 run chất lượng dừng do `time_limit`**, không dừng bởi iteration/HV. Tổng wall time chính 1.208,410s; mỗi run **60,177–60,716s**. Search overshoot lớn nhất **520ms**, total wall vượt 60s lớn nhất **0,716s**. Initialization trung vị 4–8ms tùy case/bản; số đo thời gian thật được giữ đầy đủ.

| Instance | Trung vị số vòng baseline → current | Trung vị wall time baseline → current |
|---|---|---|
| rc106_21 | 558 → 826 | 60,325s → 60,424s |
| rc205_21 | 371 → 442 | 60,444s → 60,425s |

Số vòng nhiều hơn là quan sát trong budget, không phải tỷ lệ tăng tốc theo cùng công việc: độ khó mỗi vòng và trạng thái tìm kiếm khác nhau. Không suy speedup từ việc cả hai đều đặt 60s.

Preflight cùng 200 vòng, wall time tham khảo (một mẫu mỗi bản, không phải timing repeat):

| Instance / seed | Baseline (s) | Current (s) |
|---|---:|---:|
| rc106 / 1 | 17,071 | 12,368 |
| rc106 / 2 | 33,670 | 21,933 |
| rc205 / 1 | 35,877 | 30,710 |
| rc205 / 2 | 36,834 | 32,431 |

Preflight 200 vòng và quality maxIterations 25.000 có ngưỡng thích ứng theo tổng vòng cấu hình khác nhau. Không coi preflight là prefix của quality, hoặc so trực tiếp distance giữa hai cấu hình để kết luận regression. Các phép đo tốc độ P5/P6 trước đó giữ riêng; không cộng phần trăm hoặc gộp thành một tỷ lệ tăng tốc chung mới.

## Toàn vẹn hồ sơ và quyết định

Đã kiểm tra lại **56 CSV** của 28 run: SHA khớp record, validation C++ sạch và report độc lập khớp số nghiệm/tuyến. Binary, ZIP nguồn, input và công cụ đóng băng đều giữ nguyên SHA; `final_integrity.json` đạt. Nguồn baseline đối chiếu 84 tệp, current 85 tệp production/runner/header với Git/evidence P6; số đếm current không tính CMake. Cờ tối ưu compiler giống nhau; current thêm define P6 ON.

**P7 hoàn tất theo phạm vi người dùng chốt: bổ sung rc106 và rc205**, bên cạnh bằng chứng r104/r101/c109 trước đó. Giữ cấu hình đã kiểm chứng: cache/undo/P6 ON; charging repair/profiling mặc định OFF. Không sửa solver hoặc TestALNS.cpp. Không mở thêm run 180s, sweep mọi biến thể hoặc triển khai ý tưởng P5/Inter-2-opt còn hoãn trong đợt này.

Khi đọc Git cuối đợt, người dùng đã chuyển TestALNS.cpp sang c108, scoreNonDominated 20; giữ nguyên và không stage thay đổi này. Audit dùng ALNSAuditRunner/input CLI riêng nên không bị ảnh hưởng.

Phạm vi bằng chứng: năm seed / hai case RC / 60s, cùng các đợt R/C có protocol riêng. Chưa chứng minh tối ưu toàn cục hoặc lợi ích phổ quát trên mọi instance/run dài; không gộp chúng thành benchmark năm case đồng nhất. Cấu hình audit tắt Scatter Search và dùng scoreNonDominated 25; không đại diện mọi cấu hình người dùng tự chỉnh.

Kết quả máy đọc: `update/alns_p7_rc_results.json`, `update/alns_p7_rc_results.csv`.

Raw: `artifacts/alns_p7/rc106_rc205_20261021_60s_5seeds/`. Công cụ `src/test/run_p7_rc_audit.py`, `src/test/summarize_p7_rc_audit.py`.
