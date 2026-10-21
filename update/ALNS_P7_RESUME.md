# Bàn giao P7 RC — 2026-10-21

Yêu cầu mới nhất của người dùng: chạy thêm rc106 và rc205 là đủ cho P7. Chỉ làm hai case bổ sung, giữ bằng chứng R/C trước đó riêng; không tự mở benchmark mọi instance hoặc thêm thuật toán.

## Trạng thái cuối: P7 hoàn tất theo phạm vi người dùng

Đợt chạy đã kết thúc, không còn process benchmark hoặc run cần resume. Manifest completed; summary và kiểm tra toàn vẹn đều đạt. Kế hoạch thực nghiệm đã đóng theo yêu cầu thêm hai case RC; các ý tưởng P5/Inter-2-opt tổng quát vẫn hoãn, không tự mở việc mới.

- 8 preflight / 4 cặp giữ nguyên objective/schedule byte-identical: 58 nghiệm / 446 tuyến.
- 20 quality run / 10 cặp, năm seed mỗi case: 258 nghiệm / 2.283 tuyến.
- Tổng 316 nghiệm / 2.729 tuyến đều hợp lệ C++ + Python, 0 vi phạm. Số lần xuất có tính trùng giữa các phiên bản/cấu hình.
- Fleet: rc106 cả hai bản đều 13 qua 5 seed; rc205 cả hai đều 4. Không giảm fleet, không chứng minh tối ưu.
- Theo fleet rồi distance: 4 thắng / 6 hòa / 0 thua. rc106 seed2 giảm2,8270%, seed3 giảm0,8668%; rc205 seed3 giảm0,7200%, seed4 giảm0,0852%; còn lại hòa. Trung vị mức giảm distance theo cặp là0% vì sáu cặp hòa.
- HV3D 9 thắng / 1 hòa / 0 thua, không null: rc106 thắng5/5, rc205 thắng4/5 hòa seed1. Physical scales (3120,1,240) và (3840,1,960), reference1.1 cả ba trục.
- **Đánh đổi:** rc106 seed2/3 MaxTime đại diện tăng238,158803→239,211790, vẫn dưới depot240, dù Distance/Gini tốt hơn. Đừng khẳng định mọi objective đều thắng.
- 20/20 quality stop=time_limit; wall60,176686–60,716236s, tổng1208,409921s. Search overshoot max520ms; totalwall excess max0,716236s. Median iters rc106558→826, rc205371→442, chỉ là số vòng trong budget, không gọi là speedup cùng công việc.
- Kiểm tra56CSV khớpSHA, report C++/Python khớpcounts, binary/sourceZIP/input/frozen tools giữhash. `final_integrity.json` passed.
- Giữ default cache/undo/P6ON, charging repair/profilingOFF; audit ScatterOFF. Không sửa solver hoặc user TestALNS.cpp.
- Báo cáo `update/alns_p7_rc_report.md`, kết quả `update/alns_p7_rc_results.json` + `.csv`; PLAN đã cập nhật đóng P7. R/C cũ có protocol riêng, không gộp thành benchmark5case đồng nhất hoặc cộng các phần trăm tốc độ.

## Phạm vi đã chạy

- Git nền `edd709b`, nhánh `feat/alns-charging-repair-p3-p4`; không đổi solver.
- Trước chạy đã kiểm tra Git, giữ nguyên TestALNS.cpp (r103, scoreNonDominated 20), 60 ca / 6 target CTest Release đạt lại.
- WSL Ubuntu-22.04 Release, GCC11.4. Tool `src/test/run_p7_rc_audit.py` đã hoàn tất tuần tự; session17333 đã exit0.
- Raw `artifacts/alns_p7/rc106_rc205_20261021_60s_5seeds/manifest.json`: completed, đủ8preflight/4comparisons/20quality.
- Preflight 8 run: rc106/rc205 × seed1/2 × baseline/current, 200 vòng, safety3600s; objective/schedule bắt buộc byte-identical. Sau đó 20 run / 10 cặp chất lượng: hai case × năm seed1–5 × hai bản, search60s, maxIterations25000. Pha chất lượng có thể khác vòng/front; không so byte.
- Thứ tự AB/BA cân bằng và luân phiên case, không build cạnh tranh khi đo. Kiểm chứng mọi nghiệm bằng C++ và Python replay, không reschedule. Giữ mọi run thắng/thua và overshoot.

## Binary và model

- Baseline `226f943`: evaluator đã sửa trước cache/undo/P6; SHA `59605d7c3fb148b167803b7d76d7819df1d6734085434ce00992014d28f31619`, file gốc `artifacts/alns_p5/cache_20261020/ALNSAuditRunner_before`.
- Current `edd709b`: cache2, selective undo1, P6ON; SHA `420016ebbd7dda5744da6b5ccee941fd3a770831a1484d1623f0c64709cddd25`, build `build-audit-wsl-p6-bounds/ALNSAuditRunner`.
- Charging repair/profiling/Scatter tắt cả hai; LS25, scoreNonDominated25, HVstagnation20, segment200. Không dùng cấu hình user TestALNS làm audit.
- Mỗi input có100 khách/21trạm/1depot, delivery-only Euclidean symmetric partial recharge. `data/test-100` trùng SHA `data/solomon`.
- rc106: horizon240, capacity200, battery79.69, rate1, charge time/energy.38, speed1, SHA `b33c34c1fdd755eeb864e67703b61f8b99ebc7e1b11d90c4a61cc881db39161e`.
- rc205: horizon960, capacity1000, battery194.58, rate1, charge time/energy.15, speed1, SHA `48d4a95f321146d0a11a95234da0ae0b1b6605c1fecdc8c18bbe4245a8793c4a`.
- Search60s là sau initialization, deadline mềm; báo wall/init/search/total/overshoot/stop reason, không giả vờ là60s tổng hoặc suy speedup từ thời gian hai run cùng budget.

## Hồ sơ và cách tái lập

Lệnh đã chạy: `python3 src/test/run_p7_rc_audit.py artifacts/alns_p7/rc106_rc205_20261021_60s_5seeds`, rồi `python3 src/test/summarize_p7_rc_audit.py artifacts/alns_p7/rc106_rc205_20261021_60s_5seeds update/alns_p7_rc_results.json` trong WSL. Summary ghi JSON/CSV và có kiểm tra HV analytic. Parent raw có `verify_finished.py` + `final_integrity.json` và CTest log.

Script từ chối output tồn tại, pin hash binary và Git revision; nếu solver đổi, không dùng script như thể đang kiểm chứng bản mới mà chưa cập nhật provenance. Công cụ, binary, ZIP nguồn/flags và input đóng băng trong raw. Candidate source đối chiếu85 tệp production/runner/header, baseline84 (current count không tính CMake).

Không chạy thêm180s hoặc sweep mọi biến thể vì người dùng đã chốt phạm vi hai case này. Không mở việc mới khi chưa có yêu cầu. Giữ nguyên user `src/test/TestALNS.cpp` và mọi dataset/JD/log/untracked không liên quan; luôn đọc lại diff trước tiếp tục. Commit chứa tệp bàn giao này chỉ thêm tools/docs/results/.gitignore, không thay thuật toán.

Lúc kiểm tra Git cuối đợt, người dùng đã tự chuyển `TestALNS.cpp` sang **c108**, scoreNonDominated vẫn20; thay đổi được giữ unstaged, không phải agent sửa. Nếu sau này chạy chương trình TestALNS thì cần build lại theo source hiện tại; binary TestALNS đã build ở mốc P6 trước thay đổi này. Audit P7 dùng ALNSAuditRunner/input CLI đóng băng, không bị ảnh hưởng.
