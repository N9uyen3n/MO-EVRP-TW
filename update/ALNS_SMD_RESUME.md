# SMD — đã hoàn tất đợt nâng cấp ngày 2026-10-21

Người dùng đã thay yêu cầu nghỉ bằng tiếp tục ngay; công việc sau đó đã hoàn tất. **Không còn benchmark chạy ngầm hoặc bước triển khai bắt buộc của đợt này.** Khi có yêu cầu tiếp, kiểm tra Git trước; không chạy trùng các batch completed.

## Source cuối và phạm vi

- Nhánh `feat/alns-charging-repair-p3-p4`; solver cuối **B4 `128f1cd`**, các commit docs sau đó không đổi production.
- Bài toán **EVRPTW-PR chỉ giao hàng**, Euclidean đối xứng, sạc một phần; giữ evaluator canonical và mục tiêu fleet/Pareto Distance–Gini–MaxTime.
- S1 `fc531b7`: patch top-8 và thứ tự tie/FMA nhất quán, patch bằng full rebuild.
- S2 `51ed53a`: caller-owned KNN/fallback và bỏ copy tuyến thứ hai ở intra move;384warmedcalls qua3đường đo được0allocation output.
- S3 `6e7549c`: prototype cache khối Or-opt có generation/epoch,4096entry/288KiB. **Default Off**, không bật do chưa chứng minh lợi ích runtime ổn định.
- B4 `128f1cd`: guard `removeRedundantStations(Route&)`, dùng energy screen để lọc rồi canonical-evaluate candidate toàn tuyến trước commit. Lỗi tái hiện trên D0–S7–C19–S8–C36–C49–D0 của r101; xóa S7 làm C36 arrival tối thiểu88,3995>due88.
- Regression trước guard:12pass/2fail. Sau guard **90ca/9target CTest Release đạt**; TestALNS/untitled/ALNSAuditRunner build thành công bằng WSL Ubuntu-22.04 GCC11.4 ở `build-audit-smd-s4`.
- B4 audit binary SHA256 `e8d09486bbde49bc42747750671955189e271d60d83613029125b40404fe7029`.

Giữ user `src/test/TestALNS.cpp` đang chọn **rc205_21**, scoreNonDominated20, LS15. SHA lúc build/kiểm tra cuối `36d74ad7e6190f6f18d8c883f7a19e1b7a7150ce8dbfce43a4cd68849c35408f`. Không stage userfile hay dataset/log untracked; phải đọc lại khi làm tiếp vì người dùng có thể sửa thêm. Audit độc lập dùng **LS25/score25**, reconstruction cache2, selective undo1, P6 boundsON, charging repairOFF, ScatterOFF.

## Kết quả và giới hạn cần giữ nguyên

- S2/S3/S4 fixed-iteration gates đều giữ lịch/objective byte-identical; Observe0mismatch. Enabled bỏ qua đúng3.959.545block, bớt11.062.650candidate/113.477evaluation; lợi ích timing chưa đủ để bật default.
- S5speed B1/B3Off:30/30cặp giữ output; median giảm1,479% nhưng tổng524,347→530,528s (chậm1,18%). rc205 lần đầu median−5,942%, lần lặp riêng+1,126% với khoảng−4,732..+20,006%; không gộp để đổi trọng số case.
- Tách rc205 S2/S3Off: mỗi bước4/4cặp giữ output; medianS2−1,205%,S3Off+0,835% nhưng có cặp−14,698%. Chưa xác định nguyên nhân ổn định hoặc chứng minh SMD speedup toàn solver.
- Quality S1 B0/B1:25cặp, cùng fleet; best-distance7thắng/6hòa/12thua; HV11thắng/5hòa/9thua. Giữ nguyên số liệu này, không thay bằng kết quả guard B4.
- Trace B0/B1 seed1 r101/rc106: initial giống nhau; khác đầu tiên là tie-order ở đầu VND vòng0, rồi Relocate chọn move khác. **1.037snapshot B1 khớp full-sort oracle**. Trace giữ maxIterations25000 nhưng dừng chẩn đoán8vòng; không dùng làm timing. Có4run/6nghiệm/112tuyến cuối hợp lệ, báo riêng.
- Quality B3/B4 guard:50run60s/maxIter25000,734nghiệm/8.159tuyến hợp lệ; fleet rồi best-distance **12thắng/13thua**, HV **15thắng/4thua/6không so được tại fleet tham chiếu**. r101 bớt1xe ở2seed; r104 thêm1xe ở1seed; rc106 thua best-distance5/5 nhưng thắngHV5/5. Search overshoot tối đa710ms.
- rc205 B4 đạt4xe mọi seed, best-distance tốt hơn4/5seed; ít distance nhất **1.153,15815856718**, Gini0,107666943031894,MaxTime922,289241191684. Raw đúng run `S5_station_guard_quality/rc205_21_s1_rep1_after`, **solution18**. HV tăng ởseed1,2,3,5 và giảm ởseed4.

Đã hoàn tất triển khai và đánh giá với kết luận hỗn hợp; **không coi đó là chứng minh mọi lợi ích hiệu năng đã đạt**. Giữ guard vì tính đúng, giữ S2 với lợi ích cấp phát/copy đã đo, cacheOff. Không tự tuning để thắng các seed thua; không cộng phần trăm SMD vào P5/P6 cũ.

## Hồ sơ đã lưu

- `update/alns_smd_report.md`: thí nghiệm gốc B0–B3 và liên kết phần bổ sung.
- `update/alns_smd_followup_report.md`: tách hiệu năng, trace, bằng chứng toán học lỗi xóa trạm, regression và bảng chất lượng/rc205 B4.
- JSON/CSV `update/alns_smd_*_results.*`; raw bất biến trong `artifacts/alns_smd/20261021/`, các stageB0–B4 có ZIP source,binary,flags,CMakeCache,input,CTestlog và hashes.
- `update/alns_smd_b4_final_integrity.json` **valid=true**:11experiment, **288run+8warmup**, **3.156nghiệm/33.358tuyến** hợp lệ, bao gồm lặp giữa phiên bản/seed/repetition;87file source hiện tại khớp B4, flags/binary/raw/report/userfile khớp. Trace4run được báo riêng, không cộng vào mẫu trên.
- `update/alns_smd_final_integrity.json` là chứng nhận lịch sử B3 tại checkpoint cũ; không ghi đè hoặc coi nó là chứng nhận sourceB4.
- Trace/replay gốc `build-audit-smd-scope-trace/`, kiểm tra bằng `src/test/analyze_smd_followup.py`. Chẩn đoán phát lại bằng route mới không thay thế việc kiểm tra raw schedules.

Chỉ chạy lại kiểm tra toàn vẹn khi có lý do; nếu userfile đổi thì dùng SHA của bản app mới đã build, không sửa source người dùng để khớp hash:

```text
python3 src/test/verify_smd_final.py artifacts/alns_smd/20261021 build-audit-smd-s4 update/alns_smd_b4_final_integrity.json --current-stage B4 --user-source-sha 36d74ad7e6190f6f18d8c883f7a19e1b7a7150ce8dbfce43a4cd68849c35408f
```

Các hướng ngoài scope ở mục4 của `plan.md` vẫn hoãn; chưa mở rộng cache sang toán tử khác, chưa có Routepool hoặc bộ lọc dùng slack lịch cũ làm chứng nhận. Tối ưu/tuning mới cần giả thuyết và protocol riêng, theo yêu cầu tiếp của người dùng.
