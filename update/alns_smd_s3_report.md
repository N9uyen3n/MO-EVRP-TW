# SMD S3: prototype quan sát khối Or-opt

Ngày 2026-10-21. S2 đã đạt cổng tương đương B1/B2. Prototype này **mặc định Off**; chưa có số liệu instance thật để quyết định bật cache hay kết luận tăng tốc.

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

## Việc kế tiếp

Đóng băng B3 prototype. So B2/Observe trên5case ×2seed ×200vòng, profiling bật để đo riêng cache và tổng Or-opt; yêu cầu lịch/objective giữ nguyên. Kiểm tra hit tiềm năng, evictions, memory, mismatch và overhead. Chỉ sau đó thử Enabled so Off trên cùng binary nếu có triển vọng. Quyết định default và có giữ prototype hay không sau A/B lặp ở S5.
