# Bàn giao P6 — 2026-10-21

> Mốc mới nhất: [ALNS_P7_RESUME.md](ALNS_P7_RESUME.md). Đã hoàn tất P7 trong phạm vi người dùng chốt thêm rc106/rc205; nội dung dưới giữ lịch sử mốc P6.

Người dùng đã đồng ý thử P6 trong phạm vi nhỏ sau khi xác nhận mô hình EVRPTW-PR chỉ giao hàng, partial recharge, Euclidean đối xứng. Đợt thử đã xong; không mở rộng pickup/SPD/directed. Mốc Git trước sửa `aebc888` trên nhánh `feat/alns-charging-repair-p3-p4`.

## Thay đổi và quyết định

- Cờ CMake `ALNS_INSERTION_TIME_BOUNDS` mặc định ON; OFF vẫn là đường chèn đầy đủ trước P6, không có cache/nhánh lọc trên đường chạy đó. Giá trị cache CMake cũ OFF không tự đổi theo default.
- `Route::checkInsertionCost` dùng relaxation thời gian sạc bằng 0: prefix earliest departure + suffix latest service start, O(n) xây lười, O(1) mỗi phép chèn. Chỉ loại chắc chắn trễ; ứng viên còn lại vẫn đánh giá bằng evaluator/chính sách sạc chuẩn.
- Cache `shared_ptr<const ...>` chia sẻ bất biến giữa Route copy; add/remove/reverse/clear reset, assignment mang theo cache của chuỗi được gán. Instance phải bất biến khi sử dụng Route, như cache evaluator hiện có. Tolerance bảo thủ 1e-7 so với evaluator 1e-9. Không dùng lịch/pin cũ để chặn, không chứng nhận khả thi pin bằng relaxation.
- Bộ đếm thread-local trong `insertion_time_bounds.json`: enabled, requests, builds, rejections. Reset trước solver trong audit runner; không thay ALNSConfig hoặc tham số tìm kiếm.
- Không thay Inter-2-opt, charging repair, chính sách sạc, neighborhood hay budget. Charging repair vẫn mặc định tắt. P5 cache/undo vẫn bật.

## Kiểm chứng

- Trước sửa: 54 ca / 5 target Release đạt. Sau sửa: 60 ca / 6 target đạt ở cả ON/OFF.
- Sáu ca mới `InsertionTimeBoundsTests`: deadline service-start; prefix/suffix; chèn trạm cứu base thiếu pin; invalidation sau mutations/copy/assignment; epsilon; random Euclidean. Random 400 instance / 17.346 phép chèn (2.503 feasible, 14.843 infeasible), feasibility và mọi delta khớp dựng toàn tuyến; accepted routes được replay C++.
- Đo 18 cặp / 36 run chính + 2 warmup: r104_21/r101_21/c109_21, seed 1/2, 3 lần đo lặp, 200 vòng cấu hình, safety 3.600s. Cache mode 2, selective undo 1; charging repair/profiling/Scatter tắt. Đảo AB/BA, luân phiên instance, chạy tuần tự không build cạnh tranh, giữ mọi mẫu.
- 306 nghiệm / 3.798 tuyến chính và 8 nghiệm / 96 tuyến warmup hợp lệ C++ + Python replay độc lập; objective/schedule byte-identical giữa phiên bản/lần lặp. Counters reconstruction/ejection identical.
- Trung vị giảm wall time **2,720279%**, 14/18 nhanh hơn; khoảng **−2,033822% đến +7,566222%**. Tổng **243,289999 → 236,317916s**, giảm **2,865750%**. Theo instance: r104 2,032816%; r101 2,720279%; c109 5,222196%. c109 seed 1 gần như bằng nhau, median paired −0,4545%; đừng khẳng định mọi seed thắng.
- 291.282.675 requests, 264.723.915 rejections, 15.250.458 builds; gồm timing repeats, không phải đầu vào độc lập.

Giữ và bật default vì số đo tổng/từng instance thuận lợi, lợi ích nhỏ và có bốn mẫu thua. Không cộng 2,72% vào mức khoảng 27% P5 hoặc tuyên bố tổng speedup mới khi chưa A/B trực tiếp. Số đo 200 vòng không phải prefix của 25.000 vòng do ngưỡng thích ứng. CPU WSL chỉ bổ sung; không suy luận wait time từ wall−CPU.

## Hồ sơ

- Báo cáo `update/alns_p6_insertion_bounds_report.md`, kết quả `update/alns_p6_insertion_bounds_results.json`.
- Raw `artifacts/alns_p6/insertion_bounds_20261021/experiment/`, manifest completed; ZIP nguồn, flags, hash, input, executable, logs/CSV/validation. Parent có CTest logs before/on/off.
- Baseline `aebc888` production khớp 86 tệp trong evidence `edc8e92`; executable SHA `a341cc95c3e27252e2e10ee4cc368de0b08949935be377af314df296d92d94a7`.
- P6 executable SHA `420016ebbd7dda5744da6b5ccee941fd3a770831a1484d1623f0c64709cddd25`.
- Sau benchmark chỉ đổi CMake default/comment OFF→ON và tài liệu; candidate đã biên dịch ON trong đợt đo. Raw source ZIP giữ nguyên trạng thái khi đo.
- `src/test/run_insertion_bounds_audit.py` và `summarize_insertion_bounds_audit.py`; README_AUDIT có lệnh. Script yêu cầu evidence baseline cũ còn tồn tại và thư mục output mới.
- Build ON `build-audit-wsl-p6-bounds`, OFF `build-audit-wsl-p6-off`, GCC11.4 Release WSL Ubuntu-22.04. Chương trình chạy chính dùng build ON.

## Tiếp theo

**P7 benchmark dài/thêm seed còn lại**, chưa chạy trong đợt P6. Dùng kế hoạch hiện có làm điểm xuất phát: ba instance đã chọn, năm seed, ngân sách 60s và xác nhận 180s khi cần; phải báo thời gian thực/vượt deadline, số vòng/lý do dừng, fleet trước rồi Distance/Gini/MaxTime/HV3D với chung reference. Các ý tưởng cache cặp tuyến, topology và Inter-2-opt tổng quát vẫn hoãn; không tự coi chúng là bắt buộc trước benchmark cuối.

Giữ nguyên thay đổi người dùng trong `src/test/TestALNS.cpp` (lúc đầu đợt đọc: r103, scoreNonDominated 20) và các dataset/JD/log/untracked không liên quan. Luôn đọc lại diff trước tiếp tục; audit runner dùng scoreNonDominated 25 để so công bằng. Không có yêu cầu tạm dừng mới từ người dùng.
