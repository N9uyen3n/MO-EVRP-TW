# SMD S3: prototype quan sát khối Or-opt

Ngày 2026-10-21. S2 đã đạt cổng tương đương B1/B2. Prototype **mặc định Off**. Observe và thử Enabled trên instance thật đã hoàn tất; xem kết quả và quyết định dưới đây.

## Phạm vi và hợp đồng

Đơn vị nhớ là khối `(segmentLength,r1,sourcePosition,r2)` đã duyệt hết các vị trí KNN/top-K hiện tại mà không tìm thấy cải thiện. Giữ nguyên vòng lặp first-improvement. Lookup trước KNN; chỉ ghi exhaustion sau khi vòng insertion kết thúc. Không ghi thành công hoặc block chưa xét hết.

Key gồm topology epoch, generation hai tuyến, vector index trong epoch, vị trí nguồn, độ dài, top-K, bit-pattern ngưỡng distance và trọng số distance. Những tham số khác của Or-opt hiện cố định suốt một VND; instance/vehicle bất biến trong session. Không dùng Route ID hoặc pointer. Khi mở sang operator khác phải phân tích scope riêng, không mặc định dùng lại key này.

Generation tracking đặt ở SearchContext::markDirty vốn đã được các đường commit VND sử dụng: applyMove, Inter-2-opt, Or-opt đảo đoạn, repair và energy boost. Chỉ bật tracking khi cache được cấu hình khác Off. Route tự swap/reverse nhưng centroid không đổi vẫn tăng generation. Full context rebuild tăng epoch và dựng mapping generation mới. Với structural change cùng số tuyến, caller vẫn phải invalidate như hợp đồng SearchContext. Energy boost có aggregate xóa tuyến thì patch nhận thay đổi số tuyến và rebuild trước lần lookup kế tiếp.

Cache chỉ sống logic trong một runDistanceOptimization/VND, RAII hết hiệu lực khi return/exception. Các pha charging/vehicle reduction và lần ALNS khác không tái dùng entry. Bảng trực tiếp tối đa4096entry, heap-owned, so fullkey khi hash trùng; collision chỉ mất hit. Không giữ Route trong entry.

Mode Observe luôn tính lại block, đếm repeated block/candidate/evaluation. Nếu block từng exhausted nay thành công trên cùng key, báo mismatch và throw để dừng audit. Mode Enabled chỉ được thử trên dữ liệu thật sau khi Observe cho thấy có triển vọng; default vẫn Off. Instrumentation OrOptCache dùng profiler steady-wall đã có, tắt clock khi profiling tắt. Số liệu chính thức sẽ đo cả solver ngoài scope này.

## Kiểm thử prototype

**87ca /9target CTest Release đạt**:77ca S2 +10ca cache. Bao phủ off/capacity0; so đủ từng field key; Observe không skip; collision capacity1; session exit/exception/reconfigure; unfinished/successful block không bị ghi âm; mismatch detector; khối Or-opt thật lặp lại; reverse giữ centroid, reorder/remove-add cùng số tuyến có invalidate;16fixture fullLS so Off/Observe/Enabled; giới hạn bộ nhớ và table nằm trên heap.

Các ca SearchContext thực Inter-2-opt, Or-opt đảo đoạn và reverse nay còn kiểm tra generation. Test charging repair được bổ sung kiểm tra generation của cả hai tuyến khi actual relocate/Inter-2-opt chấp nhận repair; build/check cuối của bổ sung này được lưu trong log S3.

ALNSConfig/ALNSRunStatistics và ALNSAuditRunner có mode/counters riêng. CLI thêm tham số cuối `[orOptCache=0|1|2]`; các tham số cũ giữ nguyên ý nghĩa. Bản audit cũ bỏ qua tham số0 bổ sung, runner mới ghi rõ nó trong audit_parameters.json và or_opt_cache.json.

## Kết quả Observe và thử Enabled

B3 từ `6e7549c`, binarySHA256`7caae16f1b328051e76f889ff0f5332e2f486b3fda09a72ca7f07fb70da8e46b`.

- Observe:20run/10cặp B2-vs-B3Observe,5case ×2seed ×200vòng, profileON. **178nghiệm/2.004tuyến hợp lệ;10/10cặp objective/schedule giống nhau;0mismatch**.
- Có27.079.390lookup;3.959.545block lặp;11.062.650candidate position và113.477evaluation thuộc block lặp. Tỷ lệ hit theo run11,09–22,78%.7.953.390eviction; peak4096entry. Bảng294.912byte(288KiB), chỉ cấp phát khi Observe/Enabled.
- Enabled:20run/10cặp Off-vs-Enabled trên cùng B3, profileOFF; **178nghiệm/2.004tuyến hợp lệ,10/10cặp giữ nguyên lịch/objective**.
- `verify_smd_cache_skips.py` đối chiếu Observe/Enabled trên từng case/seed: số lookup, hit, insert, eviction, session, generation, topology và thành công giống nhau; **skip đúng3.959.545block, bớt11.062.650candidate và113.477evaluation**, không bỏ nhầm block, output vẫn byte-identical.
- Một lượt timing/case/seed Enabled so Off:7cặp nhanh hơn/3chậm hơn, trung vị giảm wall0,572887%, khoảng−7,018689% đến+12,464622%. Độ dao động lớn so với trung vị; chưa đủ cơ sở khẳng định tăng tốc hoặc bật mặc định. Không gọi giảm lượt tính là giảm thời gian tương ứng.

**Quyết định: giữ cache như tùy chọn thử nghiệm, mặc định Off.** S5 so B1 với B3Off để đo thay đổi mặc định gồm S2 và chi phí đường cache tắt; không quảng bá lợi ích cacheEnabled từ phép đo này. Chưa mở cache sang toán tử khác hoặc tăng dung lượng để đuổi theo một vài run.

Raw `artifacts/alns_smd/20261021/S3_observe` và `S4_enabled`, manifest completed. Kết quả `update/alns_smd_s3_results.*`, `alns_smd_s4_results.*`, `alns_smd_s4_skip_validation.json`. Hai ứng dụng `TestALNS` và `untitled` đã build Release tại `build-audit-smd-s3`; TestALNS giữ cấu hình người dùng c108/score20/LS15, khác cấu hình audit25/25.
