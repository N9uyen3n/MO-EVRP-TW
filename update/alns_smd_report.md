# Nâng cấp SMD / LocalSearch — EVRPTW-PR

**Trạng thái: S0–S4 và 30 cặp tốc độ đã hoàn tất; 50 run chất lượng đang chạy. Kết quả thời gian hỗn hợp, cần lặp riêng rc205 trước khi chốt.**

Đợt này sửa cập nhật láng giềng bị thiếu, giảm copy KNN/trial route và thử cache khối Or-opt có phiên bản tuyến. Bài toán chỉ giao hàng, Euclidean đối xứng, sạc một phần; evaluator sạc và thứ tự mục tiêu giữ nguyên.

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

## S5 — tốc độ mặc định và phần chất lượng còn chờ

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

Theo cổng case bất định trong plan, sau đợt chất lượng sẽ lặp riêng rc205: B1/B3Off, cùng hai seed, 200 vòng, ba lần lặp, warmup riêng, tổng 12 run / 6 cặp mới. Báo cáo lần lặp riêng cùng kết quả gốc; không gộp vào trung vị chung để đổi trọng số case.

Chất lượng S1: B0 so B1, 5 case × 5 seed, search budget 60 giây, maxIterations 25.000. Dự kiến 50 run; ưu tiên fleet, sau đó distance/Gini/MaxTime và HV3D cùng scale/reference. Phải báo cả các seed giảm chất lượng hoặc đánh đổi mục tiêu.

Phần chất lượng và lặp rc205 chỉ điền số liệu sau khi manifest completed, mọi output hợp lệ và kiểm tra toàn vẹn cuối đạt. Không cộng phần trăm mới vào các mức P5/P6 trước đây.

## Hồ sơ

Raw bất biến: `artifacts/alns_smd/20261021/`. Solver cuối tương ứng B3, source `6e7549c`, audit binary SHA256 `7caae16f1b328051e76f889ff0f5332e2f486b3fda09a72ca7f07fb70da8e46b`, mặc định cache Off. Các commit sau đó về báo cáo không đổi production.

Chi tiết và điểm tiếp tục: `plan.md`, `update/ALNS_SMD_RESUME.md`, các báo cáo S1–S3 và JSON/CSV theo từng cổng. Công cụ `verify_smd_final.py` sẽ đối chiếu toàn bộ CSV, stage, source hiện tại, flags, binary, report và user-file hash sau S5.
