# Đối chiếu trực tiếp tổng thời gian trước/sau tối ưu tốc độ

Ngày đo: 2026-10-21. Mô hình EVRPTW-PR chỉ giao hàng, partial recharge, khoảng cách Euclidean đối xứng; dữ liệu r104_21/r101_21/c109_21.

## Hai bản được so sánh

- Baseline **226f943**: đã sửa evaluator/lịch sạc và các lỗi move; có công cụ profiling nhưng tắt khi đo, chưa có cache tái dựng hay selective undo ejection.
- Bản mới **edc8e92**: cache tái dựng bật mode 2 và selective undo bật; charging repair mới, profiling và Scatter Search đều tắt.

Đây là mốc trước/sau **hai tối ưu tốc độ**, không phải so với phiên bản evaluator cũ chưa được chứng nhận tính đúng. Không cộng phần trăm từ các thí nghiệm cache và undo riêng trước đây.

Hai executable Release đã đóng băng được đối chiếu SHA-256 với hồ sơ thí nghiệm trước. Nguồn đi kèm baseline khớp 84 tệp production và runner trong Git; bản mới khớp 86 tệp. Lưu ZIP nguồn của hai revision cùng executable, hash và compiler flags trong raw; không tạo thêm cây source để IDE index. Thay đổi riêng của người dùng trong TestALNS.cpp được giữ nguyên và không tham gia audit runner.

## Phương pháp

- WSL Ubuntu-22.04 / GCC 11.4 / Release, cùng flags tối ưu. Kiểm tra lại CTest hôm nay: baseline 3 target / 36 ca đạt, bản mới 5 target / 54 ca đạt.
- Hai seed 1/2 mỗi instance, **ba lần đo lặp mỗi seed**, 200 vòng cấu hình: 18 cặp / 36 run chính. Hai lượt warmup 25 vòng, seed 123, được kiểm chứng nhưng loại khỏi so thời gian.
- Mỗi cặp chạy liền nhau, đảo AB/BA; thứ tự instance luân phiên giữa các vòng lặp. Chạy tuần tự, không build hay test khác trong lúc đo. Không loại mẫu bất lợi.
- Giới hạn dự phòng 3.600 giây, yêu cầu đủ 200 vòng và dừng bởi `iteration_limit`. LS intensity 25, scoreNonDominated 25, HV stagnation limit 20; tham số tìm kiếm chung giữ nguyên.
- Wall time toàn tiến trình solver gồm khởi tạo, tìm kiếm và xuất log/CSV; validator chạy sau đó không nằm trong thời gian. Ghi thêm CPU time Linux `getrusage` như số đo phụ, không suy ra thời gian chờ từ nó.
- Kiểm tra C++ và replay CSV độc lập cho từng run; mục tiêu và toàn bộ lịch CSV phải giống nhau giữa hai bản và mọi lần lặp cùng seed/instance. Kiểm tra hash executable/input/công cụ ở cuối đợt.

Chỉ số chính là trung vị phần trăm giảm wall time của các cặp liền nhau, tính `100*(1-current/baseline)`. Phần trăm giảm tổng thời gian là chỉ số khác, có trọng số theo thời gian chạy từng workload; không trộn hai chỉ số này. Ba lần đo lặp không tạo thêm seed thuật toán độc lập.

## Kết quả

**36/36 run chính hoàn tất đúng 200 vòng**, mọi run dừng bởi `iteration_limit`; hai warmup cũng hoàn tất và hợp lệ. Có đúng **9 cặp AB và 9 cặp BA**, không loại mẫu nào. Hash executable, input và công cụ giữ nguyên; hai bộ compiler flags giống nhau.

**306 nghiệm / 3.798 tuyến của các run chính đều hợp lệ**, qua cả kiểm tra C++ và replay độc lập. Mục tiêu và lịch CSV giống từng byte giữa baseline/bản mới và qua ba lần lặp của từng seed/instance. Hai warmup có thêm 8 nghiệm / 96 tuyến hợp lệ, không tham gia các số thời gian tổng hợp bên dưới.

**Trung vị giảm wall time của 18 cặp: 26,79%**. Bản mới nhanh hơn ở **18/18 cặp**; mức giảm từng cặp từ **3,69% đến 38,41%**. Đây là số đo trực tiếp hai phiên bản, không phải cộng kết quả của cache và undo riêng.

| Instance | Số cặp (2 seed × 3 lần lặp) | Bản mới nhanh hơn | Trung vị giảm wall time theo cặp |
|---|---:|---:|---:|
| r104_21 | 6 | 6/6 | 29,12% |
| r101_21 | 6 | 6/6 | 29,95% |
| c109_21 | 6 | 6/6 | 21,37% |

Tổng wall time của các run baseline là **319,768 giây**, bản mới **234,499 giây**, tiết kiệm **85,269 giây**, tương ứng **26,67% giảm tổng thời gian**. Chỉ số này có trọng số theo thời gian workload, khác với trung vị phần trăm của 18 cặp ở trên. CPU time ghi nhận có trung vị giảm theo cặp khoảng **26,98%**, chỉ là số đo phụ trong môi trường WSL.

Các giá trị trung vị ba mẫu theo từng seed, để đọc mức thời gian thực tế:

| Instance | Seed | Trung vị baseline (s) | Trung vị bản mới (s) |
|---|---:|---:|---:|
| r104_21 | 1 | 22,365 | 17,127 |
| r104_21 | 2 | 23,405 | 16,477 |
| r101_21 | 1 | 10,967 | 8,660 |
| r101_21 | 2 | 23,004 | 14,726 |
| c109_21 | 1 | 11,765 | 9,255 |
| c109_21 | 2 | 14,225 | 11,457 |

Trung vị thời gian của từng bản và trung vị phần trăm theo các cặp liền nhau là hai phép tổng hợp khác nhau; không lấy tỷ số các ô thời gian trong bảng này để thay thế con số 26,79% của đợt đo. JSON giữ đủ cả 18 cặp, thứ tự chạy và từng số thời gian gốc để kiểm tra lại.

**Kết luận trong phạm vi thí nghiệm:** bản sau cache + selective undo giảm khoảng **27% thời gian chạy** so với baseline đã sửa đúng trước hai tối ưu này, với cùng kết quả tìm kiếm. Chưa suy rộng thành mức giảm cố định cho run 25.000 vòng hoặc toàn bộ bộ dữ liệu.

## Giới hạn và hồ sơ

Đây là hai seed trên ba instance, với số vòng cấu hình 200. Các ngưỡng thích ứng cũng dùng 200, không phải prefix của một run cấu hình 25.000 vòng. Không suy rộng con số này thành mức tăng tốc cố định cho mọi instance, run dài hoặc chế độ có charging repair/deadline khác.

Wall time vẫn chịu ảnh hưởng hệ thống/IDE/WSL. Đảo thứ tự và lặp lại giảm thiên lệch nhưng không loại hết nhiễu. Nếu dùng giới hạn thời gian thay số vòng, bản nhanh hơn có thể thực hiện nhiều vòng hơn và cho nghiệm cuối khác.

Kết quả: [alns_total_speed_results.json](alns_total_speed_results.json). Raw/source ZIP/executable/input/log/CSV/validation: `artifacts/alns_p5/total_speed_20261021_200iter_3reps/` (Git ignore). Công cụ: `src/test/run_total_speed_audit.py`, `src/test/summarize_total_speed_audit.py`; xem [README_AUDIT](../src/test/README_AUDIT.md).
