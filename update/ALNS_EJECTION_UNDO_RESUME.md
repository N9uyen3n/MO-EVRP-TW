# Bàn giao sau bước undo ejection — tạm nghỉ theo yêu cầu

Người dùng yêu cầu hoàn tất bước này rồi nghỉ, tiếp tục sáng hôm sau. Không tự mở benchmark hoặc hướng tối ưu tiếp theo.

## Mốc đã hoàn tất

- Baseline trước bước này: `0897830`, nhánh `feat/alns-charging-repair-p3-p4`.
- Giảm snapshot **Stage B** của ejection bằng checkpoint/journal các Route bị sửa, rollback ngược và commit lồng nhau. Không đổi độ sâu, ngân sách 2.000 attempts/victim, thứ tự tìm kiếm hoặc tập ứng viên.
- `ALNSConfig::useSelectiveEjectionUndo=true` mặc định; audit CLI thêm `selectiveEjection=0|1` cuối cùng. Mode full mới giữ snapshot toàn vector nhưng restore bằng move; executable cũ restore bằng copy.
- Reconstruction cache vẫn bật, charging repair và profiling vẫn tắt mặc định. Mô hình giữ EVRPTW-PR chỉ giao hàng, partial recharge, Euclidean đối xứng.
- WSL Release: 5 CTest target / **54 ca đạt**. Hai target chạy chính `TestALNS`, `untitled` build ở `build-audit-wsl-ejection-undo/`.

## Kết quả và giới hạn

- 24 run chính trên r104/r101/c109, seed 1/2, cấu hình 200 vòng; 6 run bổ sung trên r104 seed 2 để kiểm tra nhiễu thời gian.
- Tổng **30 run / 273 nghiệm / 3.321 tuyến hợp lệ**, cùng mục tiêu và lịch từng byte; bộ đếm checkpoint/commit/rollback/place/branch trials giữa hai mode mới giống nhau.
- Số tuyến lưu trong snapshot Stage B giảm **89,27–94,46%**, journal giữ nhiều nhất 6 Route. Snapshot/restore profiling giảm rõ.
- Đợt đầu selective nhanh hơn full mới 6/6 cặp, trung vị 9,27%, nhưng đây chưa là mức tăng tốc ổn định so với executable cũ: r104 seed 2 từng chậm hơn cũ 28,34%. Đo lặp giữ cả mẫu bất lợi, median ba lần là cũ 17,417s / full mới 19,682s / selective 17,368s — gần ngang nhau giữa cũ và selective. IDE/indexer có dùng nhiều CPU lúc kiểm tra; không khẳng định nguyên nhân duy nhất.
- Giữ cơ chế undo vì giảm bản sao và giữ hành vi; không cộng phần trăm này với cải thiện cache P5, không hứa runtime tổng giảm cố định.

## Hồ sơ

- `update/alns_ejection_undo_report.md`, `update/alns_ejection_undo_results.json`, `update/alns_ejection_undo_repeat_results.json`.
- Raw/source/executable: `artifacts/alns_p5/ejection_undo_20261020/experiment/`, manifest completed.
- Đo lặp: `artifacts/alns_p5/ejection_undo_20261020/repeat_r104_s2/`, manifest completed.
- Công cụ trong `src/test/`: `run_ejection_undo_audit.py`, `summarize_ejection_undo_audit.py`, `repeat_ejection_undo_audit.py`. README audit cập nhật; script benchmark các mốc cũ đã đặt selective=0 để cô lập tính năng khi đối chiếu.

## Khi được yêu cầu tiếp tục

Không có run đang chờ hay cần resume. Đọc Git/status và báo cáo trước; giữ nguyên thay đổi người dùng trong `TestALNS.cpp` (r103 và scoreNonDominated=20), các dataset/JD/log không liên quan và raw cũ. Snapshot retry Stage A, các Route trial khi thử trạm và chi phí evaluator trong ejection còn giữ nguyên; có thể đo các phần đó trước khi chọn tối ưu tiếp. P6/P7 và cache cặp tuyến/topology chưa bắt đầu; không mở rộng sang pickup hoặc directed.

Câu hỏi mới nhất của người dùng là tổng phần trăm nhanh hơn so với ban đầu. Chưa có A/B trực tiếp đủ kiểm soát để trả lời; không cộng 11,93% của cache với 9,27% của undo vì khác đối chứng và thời điểm đo. Khi được yêu cầu tiếp tục, ưu tiên so trực tiếp baseline đã sửa đúng trước tối ưu tốc độ (`226f943`) với bản cuối, cùng seed/config/ngân sách vòng và kiểm chứng lịch. Nếu người dùng muốn mốc trước sửa evaluator thì cần phân biệt tính đúng của hai phiên bản khi diễn giải tốc độ. Chưa chạy benchmark tổng này, giữ yêu cầu tạm nghỉ.
