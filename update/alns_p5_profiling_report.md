# P5 — Profiling local search trước khi chọn cache

Phạm vi: EVRPTW-PR chỉ giao hàng, sạc một phần, khoảng cách Euclidean đối xứng. Đợt này bổ sung và kiểm chứng công cụ đo; chưa triển khai cache kết quả tìm kiếm. Charging repair giữ mặc định tắt.

## Thay đổi

`ALNSConfig::profileLocalSearch` và tham số cuối `profileLS=0|1` của `ALNSAuditRunner` bật đo tùy chọn. Mặc định tắt; khi tắt, các scope đo không đọc đồng hồ hay ghi bộ đếm. Vẫn có chi phí nhánh kiểm tra cờ; không tuyên bố overhead bằng không.

Đo tích lũy trên cả lần `solve()`, gồm `LocalSearch::run`, intensified reduction được solver gọi ngoài `run`, ba pha, tám toán tử VND, các toán tử giảm xe, cập nhật context, ranking, `evaluateMove`, `applyMove` và helper tích hợp charging repair. `context_rebuild` chỉ đếm khi thật sự dựng lại context.

Mỗi mục ghi số lần gọi, thời gian inclusive, thời gian self và số lượt VND trả về cải thiện. Thời gian dùng `steady_clock`, đơn vị nanosecond, là wall time. Self loại phần inclusive của các hàm con được đo; **không cộng inclusive của cha với con**. `evaluate_move` không bao gồm các ứng viên được helper khác dựng/đánh giá trực tiếp. Chưa đo riêng copy tuyến, evaluator lõi hay từng lần gọi hàm tái dựng cũ.

## Kiểm chứng

- Git trước sửa: `2f7e923`, nhánh `feat/alns-charging-repair-p3-p4`. Giữ nguyên thay đổi của người dùng trong `src/test/TestALNS.cpp` và các dữ liệu/log sẵn có.
- Trước sửa: WSL Ubuntu-22.04, GCC 11.4, Release; 3 target CTest / 34 ca đạt.
- Sau sửa: build thư viện, audit runner và ba target kiểm thử đạt; 3 target CTest / 36 ca đạt. Hai ca mới kiểm tra phân rã thời gian lồng nhau, scope tắt, quyết định move giống nhau khi bật/tắt và reset thống kê.
- Thí nghiệm dùng executable đã copy, nguồn và input có SHA-256; chạy tuần tự, không build trong lúc đo. Bản trước sửa được bảo toàn trước khi build bản mới.

Cấu hình audit giữ như P3–P4: local search intensity 25, scoreNonDominated 25, Scatter Search tắt, HV stagnation limit 20. Không lấy cấu hình riêng trong `TestALNS.cpp` làm runner. Mỗi instance chạy seed 1/2, mỗi seed ba mode: trước sửa, bản mới tắt profiling, bản mới bật profiling. Giới hạn thời gian dự phòng 3.600 giây; script yêu cầu đủ 200 vòng và `iteration_limit`.

## Kết quả

**18/18 run đủ 200 vòng, sáu bộ trước/tắt/bật có mục tiêu giống nhau và lịch xuất giống từng byte.** Cả kiểm tra C++ và replay CSV độc lập đều đạt: tổng cộng **153 nghiệm / 1.899 tuyến, không có lỗi**. Đây là tổng các lần xuất, có tính các nghiệm lặp giữa ba mode. Manifest đã `completed`; SHA-256 của nguồn và hai executable giữ nguyên trong toàn bộ thí nghiệm.

Bảng dưới cộng hai seed trong mode bật profiling. Mẫu số là tổng inclusive của hai entry point không lồng nhau: `run` và `intensified_reduction`. Cột “giảm xe” gộp pha thường với intensified; segment exchange và ejection là các hàm con trong phần giảm xe, không cộng tiếp vào các pha.

| Instance | Tổng LS hai seed (s) | VND | Giảm xe, kể cả intensified | Segment exchange | Ejection |
|---|---:|---:|---:|---:|---:|
| r104_21 | 43,374 | 13,37% | 86,57% | 60,11% | 23,61% |
| r101_21 | 31,658 | 18,62% | 81,26% | 47,35% | 29,27% |
| c109_21 | 24,365 | 24,49% | 75,38% | 37,50% | 33,03% |

Pha xử lý trạm chỉ khoảng 0,03–0,06%. Tổng self của dựng/cập nhật context và removal ranking lần lượt là **0,108% / 0,171% / 0,131%**. Vì vậy, tối ưu riêng phần dựng context/ranking hiện có trần lợi ích rất nhỏ trong cấu hình đo này. Điều này không chứng minh cache kết quả tìm kiếm VND không có lợi; đó là một loại cache khác và chưa được đo hit rate.

Segment exchange được gọi **85 / 68 / 83 lần**, tương ứng khoảng **26,074 / 14,991 / 9,136 giây** trên r104/r101/c109. Ejection được gọi **195 / 198 / 217 lần**. Trong VND, relocate và Or-opt chiếm thời gian lớn hơn các toán tử còn lại, nhưng tổng VND vẫn thấp hơn phần giảm xe trên cả ba instance.

Wall time toàn tiến trình của mode bật so với tắt profiling có trung vị **+2,00%** trên sáu cặp, dao động **−3,70% đến +5,08%**. Bản mới tắt đo so với bản trước sửa có trung vị **−1,35%**, dao động **−14,92% đến +4,05%**. Nhiễu đáng kể và chỉ hai seed; các số này không chứng minh tăng tốc hoặc không có regression hiệu năng. Profiling giữ mặc định tắt.

## Cách diễn giải và bước cache tiếp theo

Đây là phép đo vị trí tiêu tốn thời gian, không phải một cải tiến chất lượng nghiệm hoặc một kết luận tăng tốc. Hai seed mỗi instance chưa đủ đánh giá thống kê tổng quát. Thời gian bật đo bao gồm chi phí instrumentation và nhiễu hệ thống; chênh lệch nhỏ không được coi là speedup.

Số vòng cấu hình là 200 nên các ngưỡng thích ứng và intensified reduction theo tiến độ cũng dùng 200. Kết quả không đại diện cho 200 vòng đầu của run cấu hình 25.000 vòng, hoặc cho toàn bộ run dài. Charging repair tắt trong đợt này; chưa kết luận gì về overhead khi bật repair có deadline riêng.

Trước khi cài cache, cần đo tỷ lệ lặp thực tế ở phần tốn thời gian. Nhánh segment exchange thử chuyển đoạn một đến ba khách và có thể gọi `reconstructRouteWithTwoStations(..., 4)` nhiều lần; nên đo riêng thời gian và số đầu vào lặp của helper này. Profiling hiện tại chỉ quy phần thời gian đó vào toán tử gọi, chưa chứng minh helper chiếm bao nhiêu hoặc có cache hit cao.

Nếu có tỷ lệ lặp đủ lớn, thử cache thất bại với phạm vi hữu hạn trước, giữ nguyên thứ tự duyệt và ngân sách tìm kiếm. Khóa phải xác định đầy đủ sequence tuyến, thứ tự khách thêm, thông số xe/instance và giới hạn trạm; hash cần đối chiếu nội dung để tránh collision. Nếu dùng identity/version thay sequence đầy đủ, không dùng `Route::getId()` hiện tại làm identity duy nhất. Khi chuyển nghiệm ALNS, thêm/xóa/tái dựng tuyến hoặc mở phạm vi tìm kiếm, hết hiệu lực các mục liên quan. Cần có cờ bật/tắt và đối chiếu move/lịch bằng cùng seed, số vòng trước khi đo tốc độ.

Không cache kết quả ejection chỉ theo một cặp tuyến: nó còn phụ thuộc các tuyến khác, khách đang bảo vệ, độ sâu còn lại và ngân sách thử. Ngoài ra, `patchSearchContext` hiện chỉ cập nhật tuyến bẩn và tuyến có hàng xóm cũ bị bẩn; cần kiểm tra riêng trường hợp centroid vừa đổi trở thành hàng xóm mới. Sửa phạm vi lân cận là thay đổi hành vi tìm kiếm, phải tách khỏi phép so tối ưu thuần tốc độ.

P5 mới hoàn tất phần profiling. Cache, kiểm tra hiệu lực cache và số đo tăng tốc vẫn chưa triển khai; P6–P7 chưa bắt đầu.

## Tái lập

Xem [README_AUDIT](../src/test/README_AUDIT.md). Công cụ chạy: `src/test/run_ls_profile_audit.py`; tổng hợp: `src/test/summarize_ls_profile_audit.py`. Raw, source snapshot, executable, hash, cấu hình từng run, log, CSV và kết quả kiểm chứng nằm tại `artifacts/alns_p5/profiling_20261020/experiment/` (Git ignore). Kết quả rút gọn được lưu trong [alns_p5_profiling_results.json](alns_p5_profiling_results.json).
