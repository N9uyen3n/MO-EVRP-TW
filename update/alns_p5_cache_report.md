# P5 — Cache thất bại của hàm tái dựng tuyến

Phạm vi vẫn là EVRPTW-PR chỉ giao hàng, sạc một phần, Euclidean đối xứng. Thay đổi nhằm tránh tính lặp trong hàm tái dựng cũ; không đổi thứ tự duyệt, số ứng viên, độ sâu ejection, lân cận hay điều kiện chấp nhận move.

## Cơ chế

Hàm `reconstructRouteWithTwoStations` được tách thành wrapper và phần tính toán cũ giữ nguyên. Wrapper đo riêng thời gian nếu bật profiling. Cache chỉ lưu những đầu vào mà heuristic đã trả về thất bại, không lưu tuyến thành công và không suy ra rằng bài toán con không có nghiệm khả thi.

Khóa gồm đầy đủ chuỗi node tuyến, thứ tự khách thêm, capacity/battery/energy rate của xe và `maxStations`. Dùng hash để tìm nhưng đối chiếu toàn bộ nội dung để xác nhận trùng; không dùng ID hay vị trí tuyến làm identity. Vì helper xác định chỉ phụ thuộc các đầu vào này và instance cố định, các thay đổi mục tiêu/ngưỡng VND không làm đổi ý nghĩa của một mục cache. Đổi bất kỳ nội dung đầu vào nào sẽ thành khóa khác.

Mỗi `LocalSearch::run` và `runIntensifiedVehicleReduction` mở một session riêng, xóa cache trước/sau, kể cả khi thoát bởi exception. Do đó không mang cache sang nghiệm ALNS khác hoặc lần gọi khác. Helper được gọi công khai ngoài session vẫn dùng đường cũ. Instance phải giữ nguyên trong session; thuật toán hiện tại không thay dữ liệu instance trong các lời gọi này.

Giới hạn 4.096 mục và 512 node cho mỗi khóa (gộp hai chuỗi). Bảng đầy thì xóa rồi tiếp tục tính; đầu vào vượt giới hạn chạy không cache. Giới hạn này chỉ giới hạn bộ nhớ ghi nhớ, không giảm phạm vi tìm kiếm. Ejection vẫn tính như cũ, không cache trạng thái đệ quy.

Ba mode: `Off=0`, `Observe=1`, `Enabled=2`. Observe ghi nhận các lần thất bại trùng nhưng vẫn tính lại, theo cùng chính sách giới hạn bảng; Enabled mới bỏ qua phép tính đã thất bại. Thống kê có requests, computations, successes/failures đã tính, repeated failures, hits, số insert, session, lần xóa do đầy và số mục cực đại.

## Kiểm chứng và thí nghiệm

Baseline Git trước sửa: `226f943`, nhánh `feat/alns-charging-repair-p3-p4`. Bản executable trước sửa được giữ riêng; thay đổi người dùng trong `TestALNS.cpp` không tham gia runner và được bảo toàn.

WSL Ubuntu-22.04, GCC 11.4, Release/O3/native/LTO. Trước sửa: 3 target CTest / 36 ca đạt. Sau sửa: thư viện, audit runner và 4 target CTest / **46 ca đạt**, gồm 10 ca cache mới. Kiểm tra cache hit/failure rollback, observe không bỏ tính toán, hết hiệu lực session, ID tuyến trùng, đổi thứ tự khách, giới hạn trạm, capacity/battery/energy rate, thành công không memoize, bảng đầy, đầu vào dài và đối chiếu LS có kiểm tra coverage. Fixture gọi LS trực tiếp thực hiện `evaluateRoutes()` trước khi đọc tổng nghiệm, giống luồng solver. Sau khi đo xong, hai executable chính `TestALNS` và `untitled` cũng build Release đạt tại `build-audit-wsl-p5-cache/`; không sửa cấu hình riêng của người dùng để build chúng.

Thí nghiệm **21 run**: ba run Observe có profiling (seed 1), rồi sáu bộ trước sửa/tắt cache/bật cache trên r104/r101/c109 với seed 1/2. Tất cả cấu hình 200 vòng, 3.600 giây dự phòng, LS intensity 25, scoreNonDominated 25, Scatter Search tắt, charging repair tắt. Chạy tuần tự; các run đo tốc độ tắt profiling, thứ tự off/on đảo theo seed. Nguồn, executable và input được copy, lưu SHA-256; nguồn và executable giữ nguyên trong toàn bộ đợt. Mọi run dừng đủ vòng bởi `iteration_limit`.

**Cả sáu bộ cho mục tiêu giống nhau và lịch xuất giống từng byte**, kể cả Observe. Kiểm tra C++ và replay CSV độc lập đều đạt: **168 nghiệm / 2.070 tuyến, 0 lỗi**; tổng này có tính các nghiệm xuất lặp giữa các mode. Số lần thất bại trùng ở Observe khớp chính xác số hit ở Enabled trên cả ba instance.

Thời gian dưới đây là wall time toàn tiến trình solver, không gồm thời gian validator chạy sau đó:

| Instance | Seed | Bản trước sửa (s) | Bản mới cache tắt (s) | Cache bật (s) | Giảm so với tắt |
|---|---:|---:|---:|---:|---:|
| r104_21 | 1 | 25,722 | 25,039 | 20,005 | 20,11% |
| r104_21 | 2 | 29,862 | 29,413 | 27,080 | 7,93% |
| r101_21 | 1 | 12,067 | 13,452 | 12,134 | 9,80% |
| r101_21 | 2 | 23,876 | 23,207 | 18,892 | 18,59% |
| c109_21 | 1 | 13,347 | 13,497 | 12,234 | 9,36% |
| c109_21 | 2 | 15,789 | 15,033 | 12,920 | 14,05% |

Bật cache nhanh hơn tắt trên **6/6 cặp**, trung vị giảm **11,93%**, khoảng **7,93–20,11%**. Tổng sáu run giảm từ **119,641 xuống 103,264 giây**, tương đương **13,69%** theo tổng thời gian. So trực tiếp với executable trước sửa, trung vị giảm khoảng **13,74%**, nhưng r101 seed 1 **chậm hơn 0,56%**; hai seed chưa đủ tách nhiễu thời gian hệ thống khỏi chênh lệch nhỏ. Không gọi riêng trường hợp này là cải thiện.

| Instance | Yêu cầu tái dựng, hai seed cache bật | Hit | Tỷ lệ hit | Thời gian helper / LS ở Observe seed 1 |
|---|---:|---:|---:|---:|
| r104_21 | 321.075 | 127.787 | 39,80% | 61,31% |
| r101_21 | 352.360 | 134.790 | 38,25% | 24,70% |
| c109_21 | 86.798 | 27.735 | 31,95% | 34,63% |

Tổng cộng tránh **290.312 phép tính lặp**. Peak bảng không vượt 4.096 mục, không có đầu vào quá dài trong benchmark. Bảng đầy có xảy ra trên r104/r101; xóa bảng chỉ làm mất cơ hội hit tiếp theo, không loại ứng viên. Thời gian helper là inclusive, đã nằm trong LS; không cộng hai đại lượng đó.

**Quyết định:** `ALNSConfig::reconstructionCacheMode=Enabled` mặc định. Charging repair và profiling vẫn mặc định tắt. Có thể đặt Off để đối chiếu hoặc quay đường tính toán cũ. `LocalSearch` được dùng trực tiếp vẫn cho phép cấu hình cache rõ ràng; mặc định bật trên luồng solver qua `ALNSConfig`, không tự đổi mọi đối tượng LS bên ngoài solver.

## Giới hạn kết luận

Sáu cặp seed/instance dùng số vòng cấu hình 200, không đại diện cho toàn bộ run 25.000 vòng hay tập Solomon đầy đủ. Số vòng cấu hình cũng ảnh hưởng các ngưỡng thích ứng, không phải lấy prefix của một run dài. Cache tối ưu cùng công việc tính toán; kết quả cố định số vòng mới cho phép kiểm tra điều này.

Charging repair tắt trong thí nghiệm để so đúng baseline. Nếu dùng giới hạn wall time, bản nhanh hơn có thể thực hiện nhiều vòng hơn nên nghiệm cuối có thể khác. Các helper khác có deadline riêng cũng có thể nhạy với thời gian chạy; không cam kết mọi cấu hình có thời gian giới hạn cho lịch giống từng byte.

Đợt này không sửa `patchSearchContext` hay đổi top-8 tuyến lân cận, không giảm độ sâu ejection và không thay evaluator. Đây không phải tối ưu lượng sạc hoặc mở rộng sang pickup/directed. P6–P7 chưa triển khai; cập nhật lân cận theo identity/version vẫn là công việc riêng nếu tiếp tục.

## Tái lập

Xem [README_AUDIT](../src/test/README_AUDIT.md). Công cụ chạy là `src/test/run_reconstruction_cache_audit.py`, tổng hợp là `src/test/summarize_reconstruction_cache_audit.py`. Raw và source snapshot nằm trong `artifacts/alns_p5/cache_20261020/experiment/` (Git ignore); bảng kết quả được lưu tại [alns_p5_cache_results.json](alns_p5_cache_results.json).
