# Nâng cấp SMD / LocalSearch — EVRPTW-PR

**Trạng thái: các đợt S0–S5 đã chạy đủ, gồm phép lặp rc205. Chưa đóng cổng kết luận S5 vì hiệu năng chưa ổn định và chất lượng có cả giảm. Người dùng đã yêu cầu tiếp tục điều tra ngay sau checkpoint. Cache mặc định Off.**

Đợt này sửa cập nhật láng giềng bị thiếu, giảm copy KNN/trial route và thử cache khối Or-opt có phiên bản tuyến. Bài toán chỉ giao hàng, Euclidean đối xứng, sạc một phần; evaluator sạc và thứ tự mục tiêu giữ nguyên.

**Bổ sung sau checkpoint:** các số liệu B0–B3 dưới đây được giữ nguyên. Điều tra tiếp đã có trace và hai phép tách rc205; đồng thời cô lập lỗi commit xóa trạm bằng pin của lịch cũ. Đã sửa ở **B4 `128f1cd`**, đạt 90 ca / 9 target; A/B B3/B4 đang chạy. Chi tiết và hồ sơ riêng ở `alns_smd_followup_report.md`. Chưa lấy kết quả S1 hoặc timing SMD cũ để tuyên bố chất lượng/tốc độ của B4.

## Những thay đổi đã kiểm chứng

- S1: dirty route mới tiến lại gần có thể lọt vào top-8 của tuyến clean. Patch và full rebuild dùng chung cách dựng/thứ tự, có tie-break theo index và phép tính FMA tường minh.
- S2: KNN ghi vào output buffer do caller sở hữu, xử lý cả fallback; intra move không tạo trial route thứ hai. 384 lời gọi đã warmup qua ba đường KNN/fallback không cấp phát trong phép đo.
- S3/S4: cache nhớ một khối Or-opt đã duyệt hết, có generation/epoch, key đầy đủ cho scope hiện tại, giới hạn 4.096 entry / 288 KiB. Off/Observe/Enabled đã được đối chiếu. Cache **mặc định Off**, giữ làm tùy chọn thử nghiệm.
- 87 ca / 9 target CTest Release đạt. Hai ứng dụng TestALNS/untitled đã build Release. Giữ nguyên TestALNS của người dùng, gồm c108_21, score 20 và LS intensity 15.

## Các cổng đã hoàn tất

| Cổng | Run / cặp | Nghiệm / tuyến hợp lệ | Kết quả |
|---|---:|---:|---|
| S1 phạm vi B0/B1 | 20 / 10 | 169 / 1.858 | Đủ 200 vòng; được phép thay kết quả |
| S2 tương đương B1/B2 | 20 / 10 | 178 / 2.004 | 10/10 cặp giữ nguyên objective/lịch |
| S3 Observe B2/B3 | 20 / 10 | 178 / 2.004 | 10/10 cặp giữ nguyên; 0 mismatch |
| S4 Off/Enabled cùng B3 | 20 / 10 | 178 / 2.004 | 10/10 cặp giữ nguyên; skip đúng khi đối chiếu Observe |

Enabled bỏ qua đúng 3.959.545 khối, bớt 11.062.650 candidate position và 113.477 evaluation. Tuy nhiên timing một lượt/case/seed dao động −7,02% đến +12,46%, trung vị giảm 0,57%. Chưa có bằng chứng đủ mạnh để bật mặc định; giảm lượt tính không tự động đồng nghĩa giảm wall time.

## S5 — tốc độ mặc định

Tốc độ: B1 so B3 với cache Off, 5 case × 2 seed × 3 lần lặp, 200 vòng cấu hình, warmup riêng, AB/BA cân bằng, profiling tắt. **60 run / 30 cặp hoàn tất: 534 nghiệm / 6.012 tuyến hợp lệ, 30/30 cặp giữ objective/schedule byte-identical.** Hai warmup thêm 6 nghiệm / 72 tuyến hợp lệ. Phép đo này đánh giá thay đổi mặc định sau S1, không đo lợi ích Enabled.

| Case | Cặp nhanh hơn / tổng | Trung vị giảm wall time |
|---|---:|---:|
| r104_21 | 4/6 | +3,82% |
| r101_21 | 4/6 | +1,73% |
| c109_21 | 4/6 | +1,01% |
| rc106_21 | 5/6 | +1,39% |
| rc205_21 | 2/6 | **−5,94%** |
| Tất cả cặp | 19/30 | +1,48% |

Tổng wall time **524,347 → 530,528 giây**, tăng khoảng **1,18%**. Trung vị theo cặp và tỷ số tổng thời gian đo hai đại lượng khác nhau, không thay thế cho nhau. Khoảng từng cặp −13,44% đến +5,10%. **Chưa kết luận tăng tốc toàn solver ổn định.** Giữ cả các run chậm hơn, không bỏ outlier để làm đẹp số.

Theo cổng case bất định trong plan, đã lặp riêng rc205 sau đợt chất lượng: B1/B3Off, cùng hai seed, 200 vòng, ba lần lặp, warmup riêng, tổng **12 run / 6 cặp mới**, 108 nghiệm / 432 tuyến hợp lệ; warmup thêm 6 nghiệm / 24 tuyến. **6/6 cặp giữ objective/schedule byte-identical.**

Lần lặp mới có 3/6 cặp nhanh hơn, trung vị giảm **1,126%**, khoảng **−4,732% đến +20,006%**. Tổng 198,850 → 189,973 giây, giảm 4,46%. Giữ cả cặp before 42,347 giây / after 33,875 giây tạo mức +20,006%; không loại lượt này. Lần trước rc205 có trung vị −5,942%, 2/6 cặp nhanh hơn. Dấu chậm hơn không lặp lại nhất quán, nhưng độ phân tán lớn và hai đợt trái chiều nên **chưa chứng minh lợi ích thời gian ổn định, cũng chưa xác định nguyên nhân**. Hai đợt được báo riêng; không gộp thêm sáu cặp rc205 vào trung vị 30 cặp chính để đổi trọng số case. Chi tiết `alns_smd_rc205_repeat_results.json` / `.csv`.

## S5 — chất lượng sau sửa S1

B0 so B1, 5 case × seed 1–5, search budget 60 giây, maxIterations 25.000, profiling tắt. **50 run completed, 734 nghiệm / 8.255 tuyến hợp lệ**, đều dừng vì time_limit. Search vượt deadline mềm nhiều nhất 766 ms; đây là phép so chất lượng theo thời gian, không dùng tỷ số wall time của nó làm speedup cùng công việc.

| Case | Fleet B0/B1 | Distance thắng / hòa / thua | HV3D thắng / hòa / thua |
|---|---:|---:|---:|
| r104_21 | 11/11 | 1 / 1 / 3 | 4 / 1 / 0 |
| r101_21 | 18/18 | 1 / 0 / 4 | 2 / 0 / 3 |
| c109_21 | 10/10 | 3 / 0 / 2 | 3 / 0 / 2 |
| rc106_21 | 13/13 | 2 / 0 / 3 | 1 / 0 / 4 |
| rc205_21 | 4/4 | 0 / 5 / 0 | 1 / 4 / 0 |
| Tổng 25 cặp | Cùng fleet trong mọi cặp | **7 / 6 / 12** | **11 / 5 / 9** |

Distance trong bảng là nghiệm ít distance nhất ở fleet tốt nhất; Gini/MaxTime của chính nghiệm đó được lưu trong JSON/CSV. HV dùng toàn front tại fleet chung tốt nhất của từng case, scale vật lý `(fleet × depotHorizon × speed, 1, depotHorizon)`, reference `(1.1, 1.1, 1.1)` giống nhau cho hai bản. Không gộp HV thô giữa các case.

Các đánh đổi có thật: r104 seed 1 distance tăng 0,812%, Gini giảm 0,07085 → 0,06110 và MaxTime tăng 228,828 → 229,480; seed 4 distance giảm 2,179% nhưng Gini tăng 0,05356 → 0,07240. r101 seed 2 distance tăng 2,600% nhưng Gini giảm 0,10780 → 0,07556. rc106 seed 4 distance giảm 2,522%, Gini tăng 0,05919 → 0,08807 và MaxTime giữ 238,159. rc106 có 4/5 cặp HV giảm; r101 có 4/5 cặp best-distance xấu hơn. Không được chỉ trích các seed thắng để kết luận nâng chất lượng.

S1 khôi phục hợp đồng patch bằng full rebuild, nhưng đổi phạm vi/thứ tự khám phá và quỹ đạo ALNS. Kết quả này xác nhận tính hợp lệ trong bộ thử, **không chứng minh chất lượng vượt B0**. Cần điều tra riêng ảnh hưởng trên r101/rc106 trước khi tuyên bố cải thiện chất lượng; không dùng patch cũ có thiếu cập nhật làm oracle tính đúng. Chưa có kết luận nhân quả về KNN, thứ tự move hay thành phần mục tiêu nào gây các lần thua. Mẫu 5 seed/case và ngân sách 60 giây cũng chưa đủ để khẳng định ưu thế thống kê.

Chi tiết mọi cặp và MaxTime: `alns_smd_quality_results.json` / `.csv`; manifest và raw `S5_quality`. Không cộng phần trăm mới vào các mức P5/P6 trước đây.

## Điểm dừng và phần còn mở

S1/S2 và prototype hiện ở source `6e7549c`, cache Off. S5 đã đủ dữ liệu theo protocol nhưng chưa đánh dấu hoàn tất toàn plan: còn điều tra hiệu năng rc205 và các lần giảm chất lượng r101/rc106 trước khi tuyên bố đạt mục tiêu nâng cấp. Không rollback hay bật tính năng chỉ để có số đẹp.

Người dùng ban đầu yêu cầu nghỉ sau audit, sau đó nhắn **tiếp tục ngay**. Bước đang tiếp tục: đọc `ALNS_SMD_RESUME.md`, kiểm tra Git; ưu tiên phân biệt chi phí B1/B2/B3Off trên rc205 bằng số liệu cùng công việc, rồi xem quỹ đạo tìm kiếm của S1 ở các seed chất lượng giảm. Chưa có kết luận nguyên nhân, chưa cần mở rộng cache sang toán tử khác.

## Hồ sơ

Raw bất biến: `artifacts/alns_smd/20261021/`. Solver cuối tương ứng B3, source `6e7549c`, audit binary SHA256 `7caae16f1b328051e76f889ff0f5332e2f486b3fda09a72ca7f07fb70da8e46b`, mặc định cache Off. Các commit sau đó về báo cáo không đổi production.

Kiểm tra cuối `verify_smd_final.py` **đạt (`valid=true`)**: 202 run chính + 4 warmup, tổng 2.091 nghiệm / 22.665 tuyến xuất ra hợp lệ, gồm các lần lặp giữa phiên bản/seed/repetition, không phải 2.091 mẫu độc lập. Toàn bộ CSV, stage, input/tool hashes, raw counter, report, 87 file source hiện tại, flags, binary và user-file hash đã đối chiếu. Exact cache skip validation đạt. Lỗi so sánh tuple/list JSON của công cụ kiểm tra đã được sửa; dữ liệu đo và solver không đổi.

Các session benchmark đã kết thúc, không có job của đợt này đang chờ. Chi tiết `alns_smd_final_integrity.json`, `plan.md`, `update/ALNS_SMD_RESUME.md`, báo cáo S1–S3 và JSON/CSV theo từng cổng. Chỉ sửa docs/công cụ đối chiếu sau lần CTest đạt cuối, nên không chạy lại benchmark hoặc toàn bộ CTest cho phần bàn giao.
