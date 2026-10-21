# Checkpoint SMD — ngày 2026-10-21

Người dùng ban đầu yêu cầu nghỉ sau audit, sau đó đã nhắn **tiếp tục ngay**, nên yêu cầu nghỉ đã được thay thế. Đang tiếp tục điều tra phần còn mở dưới đây. Đọc `plan.md`, báo cáo tổng hợp và Git status trước thay đổi. Bài toán EVRPTW-PR chỉ giao hàng, Euclidean đối xứng, sạc một phần.

## Trạng thái source và kiểm thử

- Nhánh `feat/alns-charging-repair-p3-p4`. Solver cuối `6e7549c`; commit báo cáo sau đó không đổi production.
- S1 `fc531b7`: sửa patch top-8, thứ tự tie/FMA; patch bằng full rebuild trong regression.
- S2 `51ed53a`: caller-owned KNN/fallback và bỏ copy tuyến thứ hai ở intra move. 384 warmed calls qua ba đường đo được 0 allocation output.
- S3 `6e7549c`: cache khối Or-opt có generation/epoch, 4096 entry / 288 KiB, Off/Observe/Enabled. **Mặc định Off.**
- Sau thay đổi production cuối: **87 ca / 9 target CTest Release đạt**, build TestALNS/untitled/ALNSAuditRunner thành công bằng WSL Ubuntu-22.04 GCC 11.4 Release.
- Giữ nguyên user `src/test/TestALNS.cpp`: c108_21, scoreNonDominated 20, LS intensity 15; SHA256 `9f0db18f208a65ccda7c0018faf492022d00512ca18a2f5714f241d9eb125a11`. Không stage file này hay dataset/log untracked của người dùng. Kiểm tra lại vì người dùng có thể sửa thêm.
- Audit dùng LS25/score25, reconstruction cache2, selective undo1, P6 bounds ON, charging repair OFF, Scatter OFF. Profiling chỉ bật cho Observe, tắt trong speed/quality chính.

## Các batch đã completed — không chạy trùng

Raw `artifacts/alns_smd/20261021/` đã Git-ignore; mỗi stage có binary, source ZIP, flags, CMakeCache, input, CTest log và hash. Các experiment có manifest, cấu hình, stop reason, counter, lịch/objective CSV và hai validator độc lập; không reschedule output để làm pass.

| Experiment | Run / cặp | Kết quả |
|---|---:|---|
| S1_scope B0/B1 | 20 / 10 | 169 nghiệm / 1.858 tuyến hợp lệ; được phép đổi quỹ đạo |
| S2_equivalence B1/B2 | 20 / 10 | 178 / 2.004 hợp lệ; 10/10 giữ lịch/objective |
| S3_observe B2/B3Observe | 20 / 10 | 178 / 2.004 hợp lệ; 10/10 tương đương, 0 mismatch |
| S4_enabled cùng B3 | 20 / 10 | 178 / 2.004 hợp lệ; 10/10 tương đương; exact skips đã đối chiếu |
| S5_speed B1/B3Off | 60 / 30 | 534 / 6.012 hợp lệ; 30/30 tương đương, warmup riêng |
| S5_quality B0/B1 | 50 / 25 | 734 / 8.255 hợp lệ; search60s / maxIter25000, tất cả time_limit |
| S5_rc205_repeat B1/B3Off | 12 / 6 | 108 / 432 hợp lệ; 6/6 tương đương, warmup riêng |

Cache Enabled bỏ qua chính xác 3.959.545 khối, bớt 11.062.650 candidate và 113.477 evaluation. Timing S4 chỉ một lượt/case/seed, median giảm 0,573%, chưa đủ để bật mặc định.

S5_speed: 19/30 nhanh hơn, median giảm 1,479%, nhưng tổng 524,347 → 530,528 giây (chậm 1,18%). rc205 lần đầu median −5,942%, 2/6 nhanh hơn. Lặp riêng rc205: median +1,126%, 3/6 nhanh hơn, khoảng −4,732% đến +20,006%; tổng 198,850 → 189,973 giây. Giữ cặp 42,347 → 33,875 giây tạo +20,006%, không bỏ outlier. Hai đợt trái chiều, chưa chứng minh hiệu năng ổn định và chưa xác định nguyên nhân. **Không gộp sáu cặp lặp riêng vào 30 cặp chính.**

S5_quality: cùng fleet ở mọi cặp, distance **7 thắng / 6 hòa / 12 thua**; HV **11 thắng / 5 hòa / 9 thua**. r101 có 4/5 best-distance xấu hơn, rc106 có 4/5 HV giảm. Search deadline mềm vượt nhiều nhất 766ms. Đây là tác động quỹ đạo của S1; không tuyên bố nâng chất lượng chung, không dùng các run 60s để đo speedup cùng công việc.

## Hồ sơ và kiểm tra cuối

- Báo cáo chính `update/alns_smd_report.md`; JSON/CSV mọi cặp trong `update/alns_smd_*_results.*`.
- Kiểm tra cuối bằng `src/test/verify_smd_final.py` đã **đạt (`valid=true`)**, kết quả `update/alns_smd_final_integrity.json`: 202 run chính + 4 warmup, 2.091 nghiệm / 22.665 tuyến hợp lệ (có lặp giữa phiên bản/seed/repetition). Các session benchmark đã kết thúc, không còn job của đợt này chạy ngầm.
- Kiểm tra đối chiếu toàn bộ stage/tool/input/CSV hashes, raw counter, derived report, warmup, source hiện tại, flags, binary và user TestALNS hash; không chạy lại solver.
- Công cụ kiểm tra cuối đã sửa so sánh tuple Python / list JSON bằng chuẩn hóa JSON ở cả hai phía; không thay số liệu hay tái sinh report để che sai khác.
- B3 audit binary SHA256 `7caae16f1b328051e76f889ff0f5332e2f486b3fda09a72ca7f07fb70da8e46b`; source `6e7549c`. B0 `0581344`, B1 `fc531b7`, B2 `51ed53a`.

Lệnh kiểm tra lại khi cần, từ workspace trong WSL (chỉ đọc raw, ghi report):

```text
python3 src/test/verify_smd_final.py artifacts/alns_smd/20261021 build-audit-smd-s3 update/alns_smd_final_integrity.json
```

## Phần đang tiếp tục

S0–S4 và các batch S5 đã xong; **chưa đóng cổng kết luận S5 của toàn plan** vì hiệu năng chưa ổn định và chất lượng có các lần giảm cần điều tra. Không có lỗi hợp lệ đã phát hiện trong bộ audit này; điều đó không chứng minh mọi instance/seed đều đúng hoặc tối ưu.

1. Kiểm tra Git và hồ sơ; không đụng TestALNS/dữ liệu riêng. Kiểm tra không còn tiến trình benchmark sống trước phép đo mới.
2. Phân biệt tác động B1/B2/B3Off trên rc205 bằng cùng công việc và cấu hình, chỉ tăng phép đo cần thiết để tìm nguyên nhân. Chưa có căn cứ quy lỗi cho compiler, máy, cache hoặc toán tử cụ thể.
3. Xem quỹ đạo và các move đầu tiên khác nhau của S1 ở seed r101/rc106 giảm chất lượng; phân biệt phạm vi top-8 đã sửa, tie/FMA và tác động thời gian. Không phục hồi patch thiếu cập nhật làm chuẩn đúng, không thay mục tiêu để làm đẹp số.
4. Cập nhật report/plan theo bằng chứng rồi mới quyết định thay đổi tiếp; giữ cache Off, chưa mở rộng sang toán tử khác. Không cộng phần trăm SMD vào mức P5/P6 cũ.

Mọi thay đổi tiếp cần commit riêng và gate phù hợp; batch đã completed có raw bất biến, runner không hỗ trợ overwrite/resume thư mục cũ.
