# Bàn giao sau A/B tổng tốc độ — 2026-10-21

> Mốc mới hơn: P6 đã thử và kiểm chứng giới hạn thời gian chèn; xem [ALNS_P6_RESUME.md](ALNS_P6_RESUME.md). Nội dung dưới giữ nguyên lịch sử đợt A/B tổng P5.

Người dùng đã yêu cầu tiếp tục sau khi tạm nghỉ. Công việc ưu tiên là trả lời tổng phần trăm nhanh hơn so với mốc trước tối ưu tốc độ; phép đo này đã hoàn tất. Không có process benchmark đang chạy hoặc run cần resume.

## Kết quả đã chốt

- So trực tiếp baseline **226f943** với bản cuối tối ưu **edc8e92**, không so với evaluator cũ chưa chứng nhận tính đúng.
- Giữ EVRPTW-PR chỉ giao hàng, partial recharge, Euclidean đối xứng. Charging repair/profiling/Scatter Search tắt ở cả hai; bản mới bật reconstruction cache mode 2 và selective undo.
- r104_21/r101_21/c109_21, seed 1/2, mỗi seed ba lần đo lặp, 200 vòng cấu hình: **18 cặp / 36 run chính**, cộng hai warmup loại khỏi thời gian tổng hợp.
- **Trung vị giảm wall time theo cặp: 26,79%**; 18/18 cặp nhanh hơn, khoảng 3,69–38,41%. Theo từng instance: r104 29,12%; r101 29,95%; c109 21,37%.
- Tổng thời gian baseline **319,768s**, bản mới **234,499s**; giảm tổng **26,67%**. Không cộng 11,93% cache với 9,27% undo từ các thí nghiệm trước.
- **306 nghiệm / 3.798 tuyến chính hợp lệ**; mục tiêu/lịch từng byte giống nhau giữa hai bản và các lần lặp. Warmup thêm 8 nghiệm / 96 tuyến hợp lệ.
- Kiểm tra lại Release hôm nay: baseline 36 ca / 3 target CTest đạt, bản mới 54 ca / 5 target đạt. Không sửa code thuật toán ở mốc A/B này.

## Hồ sơ và tính tái lập

- `update/alns_total_speed_report.md`, `update/alns_total_speed_results.json`.
- Raw: `artifacts/alns_p5/total_speed_20261021_200iter_3reps/`, manifest completed.
- Raw lưu executable, ZIP nguồn từ Git, SHA-256, input, compiler flags, môi trường, config/log/CSV và validation. Không tạo cây source clone mới; đảo 9 cặp AB và 9 BA, luân phiên instance, không build khi đo và không loại mẫu bất lợi.
- Đối chiếu source production/runner đã lưu với Git: baseline khớp 84 tệp, bản mới 86 tệp; executable khớp SHA-256 trong hồ sơ trước.
- Công cụ mới: `src/test/run_total_speed_audit.py`, `src/test/summarize_total_speed_audit.py`; README audit có lệnh chạy. Phép tổng hợp đã được kiểm tra với ví dụ có kết quả biết trước, giữ mẫu chậm hơn và từ chối manifest chưa hoàn tất.

## Giới hạn và phần còn lại

Con số khoảng 27% chỉ áp dụng cấu hình đã đo: hai seed trên ba instance, 200 vòng; các lần lặp thời gian không bổ sung seed thuật toán độc lập. Không hứa mọi run dài hoặc cấu hình có deadline khác nhanh hơn đúng mức đó. Ngưỡng thích ứng dùng tổng số vòng cấu hình, nên đây không phải prefix của run 25.000 vòng. Wall/CPU time vẫn chịu ảnh hưởng hệ thống/WSL.

Code vẫn mặc định bật cache tái dựng và selective undo; charging repair/profiling mặc định tắt. P6/P7, cache cặp tuyến và sửa topology hàng xóm chưa bắt đầu. Các snapshot retry Stage A, Route trial và evaluator trong ejection còn giữ nguyên; nếu chọn tối ưu tiếp thì đo chúng trước.

Giữ nguyên thay đổi người dùng trong `src/test/TestALNS.cpp` (lúc kiểm tra cuối hiện chọn r105, scoreNonDominated=20), các dataset/JD/log cũ và untracked không liên quan; luôn đọc lại diff khi tiếp tục vì người dùng có thể chỉnh file này. Audit runner cố định scoreNonDominated=25 và input riêng để so công bằng giữa các revision, không link TestALNS.cpp. Không mở rộng mô hình sang pickup hoặc directed.
