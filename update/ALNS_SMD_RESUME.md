# Tiếp tục SMD — 2026-10-21

Đọc `plan.md` và Git status trước khi sửa. Người dùng đã yêu cầu thực hiện plan sau giờ nghỉ; không cần xin lại chấp thuận cho các bước đã thống nhất. Bài toán EVRPTW-PR chỉ giao hàng, Euclidean đối xứng, sạc một phần.

## Đã hoàn tất

- Plan commit`0c21596`.
- S1`fc531b7`: patch top-8 đúng, deterministic tie/FMA,69ca/7target đạt; scope20run169nghiệm/1.858tuyến hợp lệ. B0/B1 được phép khác kết quả.
- S2`51ed53a`: caller-owned KNN/fallback, bỏ r2copy intra;77ca/8target đạt,384warmedcalls0allocation. Gate20run178nghiệm/2.004tuyến hợp lệ,10/10cặp B1/B2 giữ lịch/objective.
- S3`6e7549c`: cache block Or-opt bounded4096entry/288KiB, generation/epoch, Off/Observe/Enabled; **defaultOff**;87ca/9target đạt, bao phủ directVND và repairgen, session/exception/reconfigure/collision.
- Observe20run và Enabled20run đều10/10cặp tương đương,178nghiệm/2.004tuyến mỗi experiment hợp lệ. Verify exact skips:3.959.545block,11.062.650candidate,113.477eval bớt,0mismatch.
- Enabled timing chỉ1lượt/case/seed:7/10nhanh hơn,median0,572887%,range−7,018689..+12,464622%; chưa chứng minh lợi ích. Giữ **defaultOff**, lưu tùy chọn thử nghiệm.
- Build Release apps `build-audit-smd-s3/TestALNS` và `untitled` thành công. Mọi test87ca đã đạt sau thay đổi production cuối. Không cần rerun rộng khi chỉ sửa docs.

## Stage bất biến và cấu hình

Raw gốc `artifacts/alns_smd/20261021/`, đã Git-ignore. Mỗi B0/B1/B2/B3 có binary,sourceZIP,flags,CMakeCache,input5case,CTestlog và stage.json hash. Script kiểm tra lại hash trước/sau run; mọi CSV được C++ và Python validator độc lập kiểm tra, không reschedule.

| Stage | Git | Binary SHA256 |
|---|---|---|
| B0 |0581344|420016ebbd7dda5744da6b5ccee941fd3a770831a1484d1623f0c64709cddd25|
| B1 |fc531b7|f9d5a54e2b503d7c51471e73f8386ef4e45deafeb5b82b9c00cf9f2b5d2daebf|
| B2 |51ed53a|ceee55f69c23b5b1c94953d06501b2bc747eb808eb89c9374407b8707026d7af|
| B3 |6e7549c|7caae16f1b328051e76f889ff0f5332e2f486b3fda09a72ca7f07fb70da8e46b|

GCC11.4/WSLUbuntu22.04 Release flags giống nhau, P6ON. Audit:LS25/score25,cacheReconstruction2,undo1,repairOFF,ScatterOFF. S5profilingOFF,orOptCache0. B3là solver cuối hiện tại với tùy chọncacheOff, các commit docs mới hơn không đổi production.

Giữ nguyên user `src/test/TestALNS.cpp`:c108_21,scoreNonDominated20,LS15; SHA lúc buildapps `9f0db18f208a65ccda7c0018faf492022d00512ca18a2f5714f241d9eb125a11`. Không stage file này hay dữ liệu/log untracked của người dùng. Kiểm tra lại khi tiếp tục vì người dùng có thể sửa thêm.

## S5 còn lại — chạy tuần tự, không build cạnh tranh

1. Tốc độ thuần: B1 so B3Off,5case r104/r101/c109/rc106/rc205 ×seed1,2 ×3rep ×2bản=60run/30cặp;200vòng cấu hình,safety3600s;warmup riêng,AB/BA cân bằng. Bắt buộc lịch/objective byte-identical, đúng iteration_limit200. Output dự kiến `S5_speed`.
2. Chất lượng S1: B0 so B1,5case ×seed1–5 ×2bản=50run;search60s,maxIter25000. Output `S5_quality`; phải stop=time_limit, lưu overshoot. Fleet ưu tiên tuyệt đối, rồi distance/Gini/MaxTime/HV3D, báo mọi đánh đổi. Không yêu cầu byte-identical.
3. Chạy `summarize_smd_audit.py` sau mỗi experiment completed; script từ chối incomplete/invalid/modifiedCSV. Ghi JSON/CSV trong update và report tổng hợp. Các report có phần timing của scope/equivalence không được dùng thay benchmark lặp.
4. Kiểm tra source/binary/input/tool/CSV hashes cuối; source production hiện tại phải bằng B3 và userfile được giữ. `plan.md` đóng checklist, báo cáo giới hạn mẫu; commit docs/tools/results riêng. Không cộng phần trăm mới vào speedup P5/P6 cũ.

Lệnh WSL từ workspace root (phải output mới, runner không overwrite/resume thư mục có sẵn):

```text
python3 src/test/run_smd_audit.py --before artifacts/alns_smd/20261021/B1 --after artifacts/alns_smd/20261021/B3 --output artifacts/alns_smd/20261021/S5_speed --kind speed --repetitions 3 --warmup
python3 src/test/summarize_smd_audit.py artifacts/alns_smd/20261021/S5_speed update/alns_smd_speed_results.json
python3 src/test/run_smd_audit.py --before artifacts/alns_smd/20261021/B0 --after artifacts/alns_smd/20261021/B1 --output artifacts/alns_smd/20261021/S5_quality --kind quality --seeds 1 2 3 4 5 --iterations 25000 --seconds 60
python3 src/test/summarize_smd_audit.py artifacts/alns_smd/20261021/S5_quality update/alns_smd_quality_results.json
```

Hai bước chạy dài dự kiến được xếp trong một shell pipeline sau khi lưu checkpoint này. **Trước khi chạy lại phải kiểm tra manifest/progress và process đang sống; không tạo run trùng hoặc overwrite raw.** Khi shell đang chạy, cập nhật progress bằng manifest, không chạy build/benchmark song song. Tool session id nằm trong hội thoại hoạt động.
