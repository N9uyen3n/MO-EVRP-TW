# Điều tra bổ sung SMD và guard xóa trạm — 2026-10-21

Sau checkpoint `a4c6d9f`, người dùng đã yêu cầu tiếp tục ngay. Các phép tách hiệu năng và trace dưới đây đã hoàn tất; phát hiện một lỗi xóa trạm đã được sửa riêng ở **`128f1cd` (B4)**. Đã đạt 90 ca / 9 target CTest Release và build apps; audit B3/B4 đang chạy, chưa có kết luận chất lượng sau guard.

## Tách thời gian rc205

Hai experiment riêng, mỗi experiment seed1,2 × 2 lần lặp × 2 bản = 8 run / 4 cặp, 200 vòng, warmup riêng, AB/BA cân bằng, profiling OFF. Cache Or-opt Off trong mọi bản. **16/16 run hợp lệ, 144 nghiệm / 576 tuyến**, thêm 4 warmup với 12 nghiệm / 48 tuyến hợp lệ. Cả 8 cặp giữ nguyên lịch/objective và counter reconstruction/ejection.

| Bước | Nhanh hơn | Trung vị giảm wall | Tổng before → after |
|---|---:|---:|---:|
| S2: B1 → B2 | 2/4 | −1,205% | 127,255 → 129,062 s |
| S3 với cache Off: B2 → B3 | 3/4 | +0,835% | 129,737 → 132,640 s |

S2 có khoảng −4,878% đến +1,580%; S3Off −14,698% đến +3,837%. Tổng tăng lần lượt khoảng 1,42% và 2,24%, dù trung vị S3Off dương. Giữ mọi lượt đo, kể cả cặp chậm 14,698%; không gộp các experiment để thay trọng số case. Chưa có bằng chứng tăng tốc ổn định hoặc đủ căn cứ quy hiện tượng chậm rc205 cho S2 hay S3. Giảm cấp phát đã được chứng minh bằng probe/test; lợi ích wall time toàn solver vẫn chưa chứng minh. Cache tiếp tục **Off**.

## Trace khác biệt chất lượng S1

`trace_smd_scope.py` lấy ZIP source B0/B1 bất biến, thêm instrumentation trên bản sao riêng rồi build Release. Cấu hình maxIterations giữ 25.000, thêm giới hạn chẩn đoán dừng sau 8 vòng; seed1 trên r101_21/rc106_21. Trace không thay RNG, chỉ đọc node sequence/context và dựng oracle full-sort độc lập; **không dùng thời gian của trace để benchmark**. Bốn run có 6 nghiệm / 112 tuyến cuối hợp lệ qua hai validator.

Ở cả hai case, initial solution, kết quả destroy/repair đầu và đầu vào LS giống nhau. Điểm khác đầu tiên là snapshot `vnd_start` vòng0, trước patch đầu tiên: r101 có 6 hàng láng giềng đổi thứ tự/chọn cutoff, rc106 có 15 hàng. Các dãy khoảng cách bình phương vẫn giống nhau; khác biệt đầu này là xử lý tie, gồm một hàng/case đổi thành viên tại cutoff bằng nhau. Vì vậy không được quy toàn bộ ảnh hưởng chất lượng cho lỗi thiếu patch.

Ví dụ r101 route0: cuối danh sách B0 là `[25,21]`, B1 là `[21,25]`; cả hai có d²=266,6944444444444. rc106 route8 đổi `[27,20]` thành `[20,27]`, cả hai d²=181,25. Sau đó first-improvement Relocate chọn node sequence khác tại event395 (r101) và254 (rc106), vẫn ở vòng0. Sự đổi quỹ đạo xuất hiện trước deadline 60s; đây là bằng chứng thay đổi tìm kiếm, chưa phải phân rã định lượng nguyên nhân của từng lần thua ở 25 cặp chất lượng.

| Case | Snapshot context B0 | B0 snapshot khác oracle có tie-break | Quan sát B0 bỏ sót tuyến gần hơn hẳn | Snapshot B1 / khác oracle |
|---|---:|---:|---:|---:|
| r101 | 551 | 450 | 1.347 hàng | 437 / 0 |
| rc106 | 753 | 644 | 2.244 hàng | 600 / 0 |

Các hàng bỏ sót là số quan sát lặp qua các snapshot, không phải từng lỗi/tuyến độc lập. Phép đếm gần hơn hẳn dùng sai số d² 1e-8; tie-order được báo riêng. **1.037 snapshot B1 đều khớp oracle** trên chính quỹ đạo mới. Không khôi phục tie/patch cũ chỉ để giữ một seed tốt; cũng không dùng tính đúng của context để tuyên bố chất lượng luôn tăng.

## Lỗi được phát hiện ở đường xóa trạm

Trong trace có ứng viên bị chặn sau LS: r101 B0 vòng0, B1 vòng3/5; rc106 B0 vòng0. Coverage khách vẫn đủ và không trùng ở các snapshot kiểm tra. Probe phát lại node sequence bằng route mới phát hiện các thứ tự không khả thi; đây là chẩn đoán nội bộ, **không phải sửa lịch xuất ra để làm validator pass**. Một snapshot VND có thể kế thừa tuyến lỗi từ phase trước, nên không quy lỗi cho Relocate chỉ từ nhãn snapshot.

Một trường hợp đã cô lập chính xác: tuyến r101 **D0–S7–C19–S8–C36–C49–D0** hợp lệ. `removeRedundantStations(Route&)` chỉ xét pin/minBatteryReq của lịch cũ rồi xóa S7. Bỏ S7 làm pin đến S8 ít hơn, cần sạc lâu hơn; C36 đến khoảng **88,40**, vượt due **88**. Energy screen cũ vẫn pass. Hàm xóa trạm đã commit trước khi biết tuyến mới có TW không hợp lệ.

Bản sửa giữ screen làm bộ lọc, tạo trial route cho candidate qua screen, đánh giá canonical đầy đủ rồi chỉ commit khi feasible. Nếu không hợp lệ, route gốc được giữ để xét candidate khác. Giữ evaluator, mục tiêu và mô hình EVRPTW-PR delivery-only. Phần này cần O(n) trial/evaluation; không đánh đổi tính đúng để dùng slack của lịch cũ làm chứng nhận.

Trước sửa, 3 regression mới cho kết quả 1 pass / 2 fail (toàn target 12 pass / 2 fail): xóa an toàn vẫn pass; xóa upstream gây TW và chuỗi xóa an toàn rồi không an toàn đều fail. Sau sửa: **90/90 ca, 9/9 target đạt**, gồm cả ba ca mới; apps Release thành công. Commit code/test riêng `128f1cd`, build `build-audit-smd-s4`.

ALNS đã kiểm tra và loại các ứng viên nội bộ nói trên trước archive. Tất cả front đã xuất trong audit/checkpoint/trace vẫn hợp lệ; phát hiện này không phủ định các lịch đã kiểm chứng. Chưa khẳng định mọi lần reject nội bộ đều do cùng một đường xóa trạm.

## Audit sau sửa — đang chạy

Đóng băng B4 từ `128f1cd`. So B3/B4 cacheOff: 20 run phạm vi trên 5 case × seed1,2 × 200 vòng, sau đó 50 run chất lượng trên 5 case × seed1–5 × 60s / maxIter25000. Cho phép kết quả đổi vì guard thay quỹ đạo; không gọi tỷ số wall của các run khác công việc là speedup thuần. Report và raw giữ riêng, không thay dữ liệu S1.

Giữ user TestALNS đang chọn rc205_21, score20/LS15, hash lúc build B4 `36d74ad7e6190f6f18d8c883f7a19e1b7a7150ce8dbfce43a4cd68849c35408f`; audit vẫn LS25/score25. Không stage TestALNS hoặc dataset/log riêng.

Hồ sơ: `alns_smd_followup_results.json`, JSON/CSV split S2/S3; raw split trong `artifacts/alns_smd/20261021/`, trace/replay trong `build-audit-smd-scope-trace/`. Checkpoint cũ `alns_smd_final_integrity.json` chứng nhận source B3 tại lúc kiểm tra; B4 sẽ có report toàn vẹn riêng, không ghi đè chứng nhận lịch sử.
