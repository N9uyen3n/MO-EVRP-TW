# Điều tra bổ sung SMD và guard xóa trạm — 2026-10-21

Sau checkpoint `a4c6d9f`, người dùng đã yêu cầu tiếp tục ngay. **Đã hoàn tất các phép tách hiệu năng, trace, sửa guard và audit B3/B4.** Solver cuối **`128f1cd` (B4)**, 90 ca / 9 target CTest Release và apps đạt; kiểm tra toàn vẹn cuối `valid=true`. Giữ cache Off. Lợi ích runtime SMD chưa chứng minh; chất lượng có cả thắng/thua và đánh đổi mục tiêu.

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

Có thể chứng minh riêng thứ tự sau xóa không khả thi mà không dựa vào flag evaluator: C19 ready48 + service10 nên departure ≥58; S8 arrival ≥66,0623. Pin đến S8 tối đa `62,14 − sqrt(1025) − sqrt(65) = 22,0621`. Đoạn S8–C36–C49–D0 cần `sqrt(26) + sqrt(80) + sqrt(1930) = 57,9751` năng lượng và không còn trạm. Vì vậy phải sạc ≥35,9130 tại S8, mất ≥17,2382 thời gian với g=0,48. C36 arrival ≥**88,3995 > 88**. Đây là cận dưới cho mọi mức sạc một phần trên thứ tự này, không chỉ một lịch charging heuristic cụ thể.

Bản sửa giữ screen làm bộ lọc, tạo trial route cho candidate qua screen, đánh giá canonical đầy đủ rồi chỉ commit khi feasible. Nếu không hợp lệ, route gốc được giữ để xét candidate khác. Giữ evaluator, mục tiêu và mô hình EVRPTW-PR delivery-only. Phần này cần O(n) trial/evaluation; không đánh đổi tính đúng để dùng slack của lịch cũ làm chứng nhận.

Trước sửa, 3 regression mới cho kết quả 1 pass / 2 fail (toàn target 12 pass / 2 fail): xóa an toàn vẫn pass; xóa upstream gây TW và chuỗi xóa an toàn rồi không an toàn đều fail. Sau sửa: **90/90 ca, 9/9 target đạt**, gồm cả ba ca mới; apps Release thành công. Commit code/test riêng `128f1cd`, build `build-audit-smd-s4`.

ALNS đã kiểm tra và loại các ứng viên nội bộ nói trên trước archive. Tất cả front đã xuất trong audit/checkpoint/trace vẫn hợp lệ; phát hiện này không phủ định các lịch đã kiểm chứng. Chưa khẳng định mọi lần reject nội bộ đều do cùng một đường xóa trạm.

## Audit sau sửa — hoàn tất

Đã đóng băng B4 từ `128f1cd`. So B3/B4 cacheOff: **20/20 run phạm vi completed,175 nghiệm / 1.910 tuyến hợp lệ**, 5 case × seed1,2 × 200 vòng. Best fleet-distance 3 thắng / 7 thua; HV2 thắng /4 thua /4 không so được tại fleet chung tốt nhất của case. Đây là kiểm tra phạm vi 200 vòng; không thay thế quality budget60s. Cho phép kết quả đổi vì guard thay quỹ đạo; không gọi tỷ số wall của các run khác công việc là speedup thuần. Không lấy các cặp thắng của scope gate để tuyên bố nâng chất lượng.

Đợt riêng **50 run chất lượng completed: 734 nghiệm / 8.159 tuyến hợp lệ**, 5 case × seed1–5 ×60s / maxIter25000. Tất cả time_limit, search vượt deadline mềm nhiều nhất710ms. Report và raw giữ riêng, không thay dữ liệu S1.

| Case | Fleet B3 → B4 | Fleet rồi best-distance: thắng / thua | HV: thắng / thua / không so được |
|---|---|---:|---:|
| r104 | 11 → 11; riêng seed5 B4=12 | 2 / 3 | 3 / 1 / 1 |
| r101 | 18 → 18; seed2,4 B4=17 | 4 / 1 | 0 / 0 / 5 |
| c109 | 10 → 10 | 2 / 3 | 3 / 2 / 0 |
| rc106 | 13 → 13 | 0 / 5 | 5 / 0 / 0 |
| rc205 | 4 → 4 | 4 / 1 | 4 / 1 / 0 |
| Tổng25cặp | 2cặp bớt xe,1cặp thêm xe | **12 / 13** | **15 / 4 / 6** |

Fleet được ưu tiên tuyệt đối; các cặp cùng fleet so best-distance, không gọi đây là thứ tự Pareto đầy đủ. HV dùng toàn front tại fleet tham chiếu chung của case, bằng min fleet trong toàn bộ run của case, với cùng scale vật lý/reference như protocol S5. r101 có fleet tham chiếu17 nhưng B3 chỉ đạt18 nên cả5cặp không so HV ở fleet17; r104 seed5 B4 không đạt fleet11 nên thêm1cặp không so được. Không tính sáu cặp này là HV thua/hòa hoặc tùy ý thay reference sau khi thấy kết quả.

rc106 cho thấy cần báo toàn front: cả5seed best-distance xấu hơn, nhưng Gini của các nghiệm best-distance đều giảm và HV cả5cặp tăng. Ngược lại r104 seed5 thêm1xe là lần giảm chất lượng rõ theo ưu tiên fleet. Đây là mẫu5case/5seed; không kết luận bản mới tốt hơn đồng đều, không dùng số vòng hoặc wall time trong budget60s làm speedup cùng công việc.

### rc205_21 — kết quả cụ thể

Mọi run đều4xe và hợp lệ, ngân sách60s/seed.

| Seed | Distance B3 | Distance B4 | Giảm distance |
|---|---:|---:|---:|
| 1 | 1.218,765057 | **1.153,158159** | +5,383% |
| 2 | **1.198,054026** | 1.217,385088 | −1,614% |
| 3 | 1.220,393838 | **1.196,591162** | +1,950% |
| 4 | 1.207,367511 | **1.195,594956** | +0,975% |
| 5 | 1.224,747216 | **1.167,569934** | +4,668% |

Nghiệm B4 ít distance nhất: seed1,4xe,distance**1.153,15815856718**, Gini**0,107666943031894**, MaxTime**922,289241191684**. Nghiệm này là **solution18** trong raw `S5_station_guard_quality/rc205_21_s1_rep1_after/results.csv`; lịch chi tiết nằm trong `schedules.csv` cùng thư mục. ID chỉ áp dụng cho đúng run này.

HV rc205 tăng ở seed1,2,3,5 và giảm ở seed4. Seed2 distance xấu hơn nhưng HV tốt hơn; seed4 best-distance tốt hơn nhưng HV xấu hơn. Không suy chất lượng toàn front chỉ từ một nghiệm ít distance nhất.

### Chốt hồ sơ và giới hạn

`alns_smd_b4_final_integrity.json` **valid=true**: 288run chính +8warmup, tổng **3.156nghiệm /33.358tuyến** xuất ra hợp lệ; gồm các lần lặp giữa phiên bản/seed/repetition, không phải các instance độc lập. Báo cáo này đối chiếu lại toàn bộ11experiment, stage/tool/input/CSV hashes, raw stats/counters, report,87file source hiện tại, flags, binary và userfile. Exact cache skip validation của S3/S4 vẫn đạt. Bốn run trace với6nghiệm/112tuyến được báo riêng, không cộng vào mẫu timing/quality trên.

Audit binary B4 SHA256 `e8d09486bbde49bc42747750671955189e271d60d83613029125b40404fe7029`. Không còn job của đợt này chạy ngầm. Mặc định cache Off; giữ S2 với lợi ích cấp phát/copy đã đo, **không công bố SMD speedup toàn solver** vì phép đo chưa chứng minh lợi ích ổn định. Guard xóa trạm được giữ vì yêu cầu tính đúng, không phụ thuộc việc từng seed thắng hay thua.

Giữ user TestALNS đang chọn rc205_21, score20/LS15, hash lúc build B4 `36d74ad7e6190f6f18d8c883f7a19e1b7a7150ce8dbfce43a4cd68849c35408f`; audit vẫn LS25/score25. Không stage TestALNS hoặc dataset/log riêng.

Hồ sơ: `alns_smd_followup_results.json`, JSON/CSV split S2/S3; raw split trong `artifacts/alns_smd/20261021/`, trace/replay trong `build-audit-smd-scope-trace/`. Checkpoint cũ `alns_smd_final_integrity.json` chứng nhận source B3 tại lúc kiểm tra; B4 sẽ có report toàn vẹn riêng, không ghi đè chứng nhận lịch sử.
