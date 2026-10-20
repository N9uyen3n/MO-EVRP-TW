# r104_21 không giới hạn thời gian — kết quả đã kiểm chứng

Ngày chạy: 2026-10-20. EVRPTW-PR chỉ giao hàng, sạc một phần, khoảng cách Euclidean đối xứng. Dữ liệu `data/solomon/r104_21.txt`: 100 khách, 21 trạm. SHA256 `23e5797d46442f75628fe9dc35f7b448898eccd801294eca85eb48b3dc6997be`.

## Kết quả

Run kết thúc ở **6.629 vòng**, lý do `user_requested_stop`, exit code 0. Cả validator C++ và replay CSV độc lập xác nhận **52/52 nghiệm, 572 tuyến, 0 lỗi**. Không sửa lịch hoặc lượng sạc sau chạy.

| Chỉ số của nghiệm đại diện (ít xe nhất rồi distance nhỏ nhất) | Giá trị |
|---|---:|
| Solution ID trong results.csv (từ 0) | 27 |
| Số xe | 11 |
| Distance | 1073.94204419153 |
| Gini theo active time | 0.0639201169199035 |
| MaxTime | 228.571714345893 |
| Wall time monotonic | 937.5555 giây, khoảng 15 phút 37.6 giây |

Runner dùng steady_clock ghi 937.407 giây, gần khớp phép đo tiến trình. Bộ đếm nội bộ solver dùng high_resolution_clock ghi 1008.996 giây, chênh khoảng 71.6 giây; chưa xác định nguyên nhân dịch đồng hồ. Không dùng giá trị đó để khẳng định tốc độ. Trong P3–P4 sau đó, giới hạn/bộ đếm vòng ALNS đã chuyển sang steady_clock cho cả hai mode.

## Cấu hình và diễn biến

- Baseline `646ff91` đã sửa P0–P2, chưa có charging repair P3–P4. Seed 1, maxIterations 25.000, LS intensity 25, Scatter Search tắt.
- Ban đầu bỏ giới hạn thời gian: runner riêng dùng maxTime=0 và guard tắt time stop. Tắt cả HV early stop để có thể chạy đủ vòng; các pha tìm kiếm vẫn theo maxIterations=25.000.
- Người dùng sau đó yêu cầu nếu tới 10.000 vòng không cải thiện thì dừng. Bản đang chạy chưa nhận lệnh dừng gọn nên được tạm giữ nguyên bộ nhớ/log; bản riêng có checkpoint và signal handler được build rồi tái lập cùng seed.
- Yêu cầu mới nhất cho phép chuyển bước nếu nghiệm hiện tại hợp lệ. Run tái lập dừng bằng SIGTERM có handler sau khi bắt kịp mức tiến triển cũ. Handler yêu cầu thoát ở ranh giới vòng rồi solver xuất archive và kiểm chứng bình thường; số vòng thực là 6.629.
- Evolution log khớp **1.718.580 byte prefix** run cũ. Sau khi nghiệm mới đạt kiểm chứng, run cũ đang SIGSTOP đã kết thúc; không có run cũ cần phục hồi. pause_note.json ghi `superseded_by_validated_replay`.
- WSL Ubuntu-22.04, GCC 11.4, CMake Release, O3/native/LTO. Ba target CTest và smoke test dừng checkpoint/signal đều đạt.

## Kiểm chứng

`src/test/validate_audit_schedule.py` đọc trực tiếp CSV đủ độ chính xác và dữ liệu instance, không gọi Route evaluator, không sửa lịch. Kiểm tra pin trước/sau sạc, bảo toàn năng lượng, tải giao hàng, TW bắt đầu phục vụ, giờ hoàn tất sạc, phục vụ mỗi khách đúng một lần, Distance/Gini/MaxTime.

Đã đối chiếu validator này với dữ liệu P0–P2: trước sửa phát hiện 11/11 nghiệm lỗi ở c101C5 seed1, sau sửa 0/11 nghiệm lỗi. Với run r104 này, cả 52 nghiệm đều đạt; validation.txt của C++ rỗng.

## Bằng chứng và tái hiện

Thư mục: `artifacts/alns_p3_p4/r104_conditional_seed1_20261020/`.

- manifest.json, summary.json, finalization.json, ctest.txt.
- output/results.csv, output/schedules.csv, output/run_statistics.json, output/validation.txt.
- independent_validation.json, independent_validation_console.txt.
- Patch so với 646ff91, header/test checkpoint/signal stop được giữ trong thư mục. Source tại `build-audit-r104-conditional-source-646ff91`, build tại `build-audit-r104-conditional-release`.

Run gốc tại `artifacts/alns_p3_p4/r104_unlimited_seed1_20261020/` chỉ giữ để truy vết.

```powershell
python src/test/validate_audit_schedule.py artifacts/alns_p3_p4/r104_conditional_seed1_20261020/r104_21.txt artifacts/alns_p3_p4/r104_conditional_seed1_20261020/output artifacts/alns_p3_p4/r104_conditional_seed1_20261020/recheck_new.json
```

Đây là một seed để xác nhận tính đúng baseline, chưa chứng minh tối ưu hoặc cải thiện tổng quát. Không so trực tiếp run dài này với các run P3–P4 chỉ có 60 giây; xem bảng cùng ngân sách trong `alns_p3_p4_report.md`.
