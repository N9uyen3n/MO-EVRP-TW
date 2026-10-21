# Checkpoint SMD — ngày 2026-10-21

Người dùng ban đầu yêu cầu nghỉ sau audit, sau đó đã nhắn **tiếp tục ngay**, nên yêu cầu nghỉ đã được thay thế. Đang tiếp tục điều tra phần còn mở dưới đây. Đọc `plan.md`, báo cáo tổng hợp và Git status trước thay đổi. Bài toán EVRPTW-PR chỉ giao hàng, Euclidean đối xứng, sạc một phần.

## Công việc hiện tại sau checkpoint a4c6d9f

- Đã completed cả `S5_rc205_S2_split` và `S5_rc205_S3_split`: 16 run / 8 cặp giữ output, thêm 4 warmup, mọi front hợp lệ. S2 median −1,205%, S3Off +0,835% nhưng một cặp −14,698%; chưa chứng minh tăng tốc ổn định.
- Trace B0/B1 trong `build-audit-smd-scope-trace` đã completed: 4 run, 6 nghiệm / 112 tuyến hợp lệ; 1.037 context snapshot B1 khớp oracle. Cấu hình maxIterations25000, stop chẩn đoán8 vòng, seed1, r101/rc106. Khác biệt đầu tiên là tie-order ở VND đầu vòng0, sau đó Relocate chọn move khác. Trace/replay chỉ chẩn đoán, không dùng làm benchmark.
- Phát hiện và sửa riêng guard xóa trạm ở **`128f1cd` (B4)**: energy screen của lịch cũ không bảo đảm TW khi sạc lại. Regression trước sửa 12 pass / 2 fail, sau sửa **90 ca / 9 target đạt**, apps Release build xong ở `build-audit-smd-s4`. Current audit binary SHA256 `e8d09486bbde49bc42747750671955189e271d60d83613029125b40404fe7029`.
- **Session WSL 50453 đang chạy**: freeze B4 đã xong, `S5_station_guard_scope` B3/B4 cacheOff (20 run, 5 case × seed1,2,200vòng), summarize, rồi `S5_station_guard_quality` (50 run, 5 case × seed1–5,60s/maxIter25000), summarize và final integrity B4. Chưa được chốt chất lượng sau guard khi pipeline chưa completed. Không build hoặc chạy benchmark cạnh tranh.
- Đọc manifest/process trước khi chạy; runner từ chối thư mục output có sẵn. Các kết quả bổ sung tách khỏi checkpoint 202 run phía dưới. Báo cáo chi tiết `update/alns_smd_followup_report.md`, JSON `update/alns_smd_followup_results.json`.

## Trạng thái source và kiểm thử

- Nhánh `feat/alns-charging-repair-p3-p4`. Solver B3 ở checkpoint cũ `6e7549c`; **solver hiện tại B4 `128f1cd`**, đã thêm guard xóa trạm. Commit báo cáo không đổi production.
- S1 `fc531b7`: sửa patch top-8, thứ tự tie/FMA; patch bằng full rebuild trong regression.
- S2 `51ed53a`: caller-owned KNN/fallback và bỏ copy tuyến thứ hai ở intra move. 384 warmed calls qua ba đường đo được 0 allocation output.
- S3 `6e7549c`: cache khối Or-opt có generation/epoch, 4096 entry / 288 KiB, Off/Observe/Enabled. **Mặc định Off.**
- B3 đạt 87 ca; sau guard B4 đạt **90 ca / 9 target CTest Release**, build TestALNS/untitled/ALNSAuditRunner thành công bằng WSL Ubuntu-22.04 GCC 11.4 Release.
- User `src/test/TestALNS.cpp` đã đổi từ c108_21 sang **rc205_21**, scoreNonDominated20, LS15; SHA lúc build B4 `36d74ad7e6190f6f18d8c883f7a19e1b7a7150ce8dbfce43a4cd68849c35408f`. SHA ở checkpoint B3 cũ là `9f0db18f208a65ccda7c0018faf492022d00512ca18a2f5714f241d9eb125a11`. Không stage file này hay dataset/log untracked. Kiểm tra lại vì người dùng có thể sửa thêm.
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
- Kiểm tra cuối checkpoint B3 đã **đạt (`valid=true`)**, `update/alns_smd_final_integrity.json`: 202 run + 4 warmup, 2.091 nghiệm / 22.665 tuyến hợp lệ (có lặp). Đây là chứng nhận lịch sử source6e, không chứng nhận B4. Các batch cũ đã kết thúc; batch B4 hiện có session50453 phía trên.
- Kiểm tra đối chiếu toàn bộ stage/tool/input/CSV hashes, raw counter, derived report, warmup, source hiện tại, flags, binary và user TestALNS hash; không chạy lại solver.
- Công cụ kiểm tra cuối đã sửa so sánh tuple Python / list JSON bằng chuẩn hóa JSON ở cả hai phía; không thay số liệu hay tái sinh report để che sai khác.
- B3 audit binary SHA256 `7caae16f1b328051e76f889ff0f5332e2f486b3fda09a72ca7f07fb70da8e46b`; source `6e7549c`. B0 `0581344`, B1 `fc531b7`, B2 `51ed53a`.

Lệnh kiểm tra lại khi cần, từ workspace trong WSL (chỉ đọc raw, ghi report):

```text
python3 src/test/verify_smd_final.py artifacts/alns_smd/20261021 build-audit-smd-s4 update/alns_smd_b4_final_integrity.json --current-stage B4 --user-source-sha 36d74ad7e6190f6f18d8c883f7a19e1b7a7150ce8dbfce43a4cd68849c35408f
```

## Phần đang tiếp tục

S0–S4 và các batch S5 gốc đã xong; **chưa đóng toàn plan vì audit sau guard B4 đang chạy**. Hiệu năng SMD vẫn chưa chứng minh ổn định, quality S1 có đánh đổi và đã có trace tìm điểm khác đầu tiên. Mọi front đã xuất và kiểm chứng đều hợp lệ; các ứng viên nội bộ lỗi xóa trạm đã bị ALNS chặn. Điều đó không chứng minh mọi instance/seed đều đúng hoặc tối ưu.

1. Kiểm tra Git và hồ sơ; không đụng TestALNS/dữ liệu riêng. Kiểm tra không còn tiến trình benchmark sống trước phép đo mới.
2. Chờ session50453, đọc summary scope và quality; nếu audit thất bại, giải quyết đúng lỗi và giữ raw failed, không overwrite. Đánh giá B3/B4 là thay đổi quỹ đạo, không yêu cầu byte-identical hoặc lấy thời gian khác công việc làm speedup.
3. Final integrity B4 kỳ vọng 288 run chính / 8 warmup, không gồm 4 run trace. Nếu user TestALNS đổi hash trong lúc audit, chỉ rebuild app sau benchmark kết thúc và dùng SHA mới; không ghi đè user source. Các binary audit đã đóng băng vẫn độc lập.
4. Lưu báo cáo, JSON/CSV, chốt kết luận với mọi trade-off; không hứa tăng tốc/chất lượng chung nếu số liệu không chứng minh. Giữ cache Off, không mở thêm toán tử hoặc tuning ngoài plan. Không cộng phần trăm SMD vào P5/P6 cũ.

Mọi thay đổi tiếp cần commit riêng và gate phù hợp; batch đã completed có raw bất biến, runner không hỗ trợ overwrite/resume thư mục cũ.
