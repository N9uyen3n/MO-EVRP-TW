# Trạng thái ALNS sau P3–P4 — 2026-10-20

P3–P4 đã hoàn tất triển khai và kiểm chứng. Không có run ALNS cần tiếp tục hoặc tiến trình tạm giữ cần phục hồi.

## Phạm vi

- EVRPTW-PR: chỉ giao hàng, cửa sổ thời gian, sạc một phần; khoảng cách Euclidean đối xứng của Solomon.
- Không thêm pickup/SPD hoặc yêu cầu directed/JD. Các regression directed cũ P0–P2 là lịch sử kiểm tra; 14 fixture P3 mới hoàn toàn Euclidean đối xứng.
- HMA chỉ là nguồn ý tưởng về phục hồi trạm, không so chất lượng trực tiếp với HMA-SPD.

## Git và code

- Nhánh `feat/alns-charging-repair-p3-p4`, baseline P0–P2 là `646ff91`.
- Code P3–P4: **28a9057ed71245851249810b6ac8c2b8805628bb**. Bounded repair, widening trong cùng budget, cache detour, relocate/inter-2-opt nguyên tử, bộ đếm, steady_clock, CLI bật/tắt, 14 ca mới.
- `useChargingRepairLS=false` mặc định; chưa có bằng chứng để tự bật mặc định.
- WSL Release: toàn bộ target build đạt; CTest 3/3 target / 34 ca đạt. Source/executable đóng băng và hash không đổi trong lúc benchmark.

## Kết quả

1. Log cũ r103: 49/49 lịch xuất có lỗi, nhưng giữ nguyên thứ tự khách/trạm thì 49/49 bộ tuyến lập lại được lịch khả thi. Distance/14 xe xác nhận; Gini cũ chưa chứng nhận. Xem `update/audit_r103_21_legacy_report.md`.
2. r104 không giới hạn thời gian: theo yêu cầu mới nhất, dừng gọn để kiểm chứng rồi chuyển bước. Tái lập cùng seed, dừng ở **6.629 vòng**, 52/52 nghiệm / 572 tuyến qua cả C++ và replay CSV độc lập. Đại diện: 11 xe, distance 1073.94204419153, Gini 0.0639201169199035, MaxTime 228.571714345893. Run gốc tạm giữ đã kết thúc; không phục hồi. Xem `update/r104_unlimited_run.md`.
3. P3–P4: **18 run** = r104/r101/c109 × seed 1/2/3 × off/on, 60 giây/run, tuần tự cùng executable. **221 nghiệm / 2.792 tuyến, 0 lỗi**. Preflight off khớp từng byte với baseline 646ff91 trên ba ca C/R/RC, seed 1, 25 vòng.
4. Distance đại diện on tốt hơn 4/9 cặp, kém hơn 5/9; HV3D chung tốt hơn 6/9. Số xe cuối không đổi: r104=11, r101=18, c109=10. Có 155 relocate nhận qua repair; chưa có accepted Inter-2-opt trong benchmark, dù unit fixture xác nhận hoạt động. Giữ default tắt.

Báo cáo: `update/alns_p3_p4_report.md`, JSON/CSV cạnh đó. Raw và snapshot tại `artifacts/alns_p3_p4/paired_euclidean_60s_20261020/` (ignore), manifest completed. Không chạy lại trùng thư mục.

## Công cụ

- `validate_audit_schedule.py`: stdlib, replay trực tiếp full-precision CSV, không sửa lịch.
- `validate_legacy_front.py` + test tương ứng: NumPy/SciPy cho log làm tròn; LP không nằm trong solver.
- `run_charging_repair_audit.py`: preflight rồi 18 run tuần tự, kiểm chứng mọi nghiệm.
- `summarize_charging_repair_audit.py`: thống kê đầy đủ, HV3D với reference vật lý chung; ví dụ union volume giải tích đạt.
- `src/test/README_AUDIT.md`: hướng dẫn chạy lại.

## Giữ thay đổi người dùng

- TestALNS.cpp do người dùng đổi sang r103 và scoreNonDominated 25→20. Không sửa hoặc stage file này. Audit cố định 25 để so baseline; không dùng TestALNS.
- Giữ các untracked user files: .idea/vcs.xml, JD_DATA_TRANSFORMATION.md, build-jd, data datasets, logs_jd, main_final.tex, script JD cũ.
- Raw legacy ở `update/audit_r103_21_legacy/` vẫn giữ để không làm hỏng link đã đưa người dùng; raw này chưa nhất thiết nằm trong Git.

P5–P7 chưa triển khai. Nếu được yêu cầu tiếp, profiling chi tiết trước khi chọn cache/cập nhật gia tăng. Giữ nguyên phạm vi mô hình; không tune theo seed vừa đo rồi coi cùng bảng là đánh giá độc lập. Chưa chứng minh tối ưu hoặc ưu thế tổng quát.
