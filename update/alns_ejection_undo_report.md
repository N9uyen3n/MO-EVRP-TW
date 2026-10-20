# Giảm snapshot trong Stage B của ejection chain

Mô hình: EVRPTW-PR chỉ giao hàng, sạc một phần, Euclidean đối xứng. Baseline Git trước sửa là `0897830`; giữ nguyên thay đổi người dùng trong `src/test/TestALNS.cpp` và các dữ liệu/log sẵn có.

## Thay đổi

Stage B trước đây sao chép toàn bộ `workRoutes` ở mỗi nhánh đệ quy và mỗi lần tăng độ sâu, rồi copy toàn bộ trở lại nếu thất bại. `EjectionUndo` mới dùng checkpoint lồng nhau: trước lần sửa đầu tiên của một tuyến trong checkpoint, lưu bản sao tuyến đó; khi thất bại, khôi phục theo thứ tự ngược bằng move. Nhánh con đã commit vẫn có thể bị checkpoint cha rollback. Khi toàn bộ phép chèn thành công, giữ thay đổi và hủy lịch sử undo trước khi xử lý khách tiếp theo.

Tuyến được lưu gồm toàn bộ sequence, lịch và trạng thái lazy/cached, không chỉ danh sách khách. Chỉ số tuyến được dùng trong journal vì Stage B không chèn/xóa tuyến; journal kết thúc trước khi commit các tuyến vào nghiệm. Độ sâu vẫn chặn ở 5 nên số bản sao đang giữ trong journal tối đa 6 tuyến, không phụ thuộc tổng số xe.

Có cờ `useSelectiveEjectionUndo` và tham số cuối `selectiveEjection=0|1` của audit runner. Mode 0 vẫn lưu cả danh sách tuyến như đối chứng; khi rollback, mode đối chứng mới chuyển vector snapshot trở lại bằng move, còn executable cũ copy lại. Vì vậy có cả so sánh với executable baseline riêng để thấy tác động tổng thể.

Không đổi thứ tự victim/ứng viên, regret, top-K, độ sâu hoặc giới hạn 2.000 attempts/victim. Giữ nguyên các snapshot retry Stage A, bản sao workRoutes ban đầu và các Route trial khi thử trạm. Thống kê/timer `ejection_snapshot` và `ejection_restore` chỉ đo checkpoint **Stage B**, không phải mọi copy trong ejection.

## Kiểm chứng

Trước sửa: WSL Ubuntu-22.04, GCC 11.4, Release/O3/native/LTO, 4 target CTest / 46 ca đạt. Sau sửa: build thư viện, audit runner và 5 target CTest / **54 ca đạt**. Tám ca mới gồm snapshot một lần cho một tuyến trong checkpoint, commit con/rollback cha, sửa cùng tuyến ở nhiều mức, commit toàn bộ, rollback khi exception, cache đánh giá đang dirty và đối chiếu ejection thực tế trên dữ liệu Euclidean qua các độ sâu.

Thí nghiệm có 24 run trên r104/r101/c109, seed 1/2, cấu hình 200 vòng. Sáu run profiling ở seed 1 đo hai mode; 18 run tắt profiling so executable trước sửa / mode full / mode selective. Reconstruction cache giữ bật mode 2, charging repair tắt, LS intensity 25, scoreNonDominated 25, Scatter Search tắt. Thời gian dự phòng 3.600 giây, yêu cầu đủ vòng và `iteration_limit`.

Đóng băng source, executable và input cùng SHA-256, chạy tuần tự và không build khi đo. Đối chiếu mục tiêu và lịch CSV từng byte; replay vật lý độc lập. Giữa hai mode mới còn yêu cầu giống số checkpoints, commit/rollback, lời gọi `place` và branch trials. Đây là kiểm tra để không đổi lượng công việc tìm kiếm nhằm tạo lợi thế tốc độ.

Đợt đầu đã hoàn tất: **24/24 run đủ vòng**, **183 nghiệm / 2.241 tuyến hợp lệ** qua cả C++ và replay độc lập. Cả sáu bộ trước/full/selective có mục tiêu giống nhau và lịch CSV giống từng byte, kể cả các run profiling. Hai mode mới có số checkpoint, commit/rollback, lời gọi `place` và nhánh thử giống nhau trong mỗi seed/instance. Đây là tổng lượt xuất, có tính các nghiệm lặp giữa mode.

Số tuyến được sao chép để lưu checkpoint Stage B, cộng hai seed tắt profiling:

| Instance | Full | Selective | Giảm số tuyến được lưu |
|---|---:|---:|---:|
| r104_21 | 16.110.346 | 1.446.955 | 91,02% |
| r101_21 | 29.161.740 | 1.616.998 | 94,46% |
| c109_21 | 15.943.420 | 1.710.776 | 89,27% |

Journal không vượt **6 tuyến đang lưu**, khớp giới hạn độ sâu. Trong các run profiling seed 1, tổng inclusive của snapshot + restore lần lượt giảm từ **1,026 xuống 0,118 giây** (r104), **2,228 xuống 0,225 giây** (r101), **0,919 xuống 0,126 giây** (c109). Phần này chiếm khoảng 5,58% / 18,47% / 9,55% tổng LS ở mode full; đây là số đo có instrumentation, không dùng thay cho phép so tốc độ tắt profiling.

Wall time toàn tiến trình trong đợt đầu, profiling tắt:

| Instance | Seed | Executable cũ (s) | Full mới (s) | Selective (s) |
|---|---:|---:|---:|---:|
| r104_21 | 1 | 19,075 | 16,574 | 16,133 |
| r104_21 | 2 | 17,282 | 24,821 | 22,180 |
| r101_21 | 1 | 17,230 | 14,033 | 8,747 |
| r101_21 | 2 | 18,475 | 17,520 | 15,009 |
| c109_21 | 1 | 10,651 | 10,148 | 9,348 |
| c109_21 | 2 | 12,810 | 11,854 | 11,210 |

Selective nhanh hơn full mới **6/6 cặp**, trung vị giảm **9,27%**, khoảng **2,66–37,67%**. So với executable cũ, năm cặp nhanh hơn nhưng **r104 seed 2 chậm hơn 28,34%**. Vì dao động lớn và lúc kiểm tra có IDE/indexer dùng nhiều CPU, không lấy các mức tăng/giảm đơn lẻ này làm mức tăng tốc ổn định. Không loại bỏ kết quả bất lợi khỏi bảng.

Đo lặp riêng trường hợp r104 seed 2 thêm hai lần/mode, đổi thứ tự full/trước/selective rồi selective/trước/full, cùng executable đã đóng băng. Cả **6 run bổ sung / 90 nghiệm / 1.080 tuyến đều hợp lệ**, lịch/mục tiêu khớp bản gốc và các bộ đếm nhánh giữ nguyên. Tổng hai đợt: **30 run / 273 nghiệm / 3.321 tuyến, 0 lỗi**.

Trung vị wall time của ba mẫu mỗi mode (gồm mẫu đầu bất lợi, không loại bỏ): executable cũ **17,417 giây**, full mới **19,682 giây**, selective **17,368 giây**. Selective chỉ thấp hơn bản cũ **0,29%**, chưa phải tăng tốc rõ rệt ở trường hợp này. CPU time Linux `getrusage` ghi nhận trong hai lần mới/mode có trung vị **19,209 / 20,366 / 18,522 giây** tương ứng, selective thấp hơn bản cũ khoảng **3,58%**. Đây là số đo thô trong WSL, không dùng để suy ra thời gian chờ hay loại trừ mọi ảnh hưởng của hệ thống. Chi tiết: [kết quả đo lặp](alns_ejection_undo_repeat_results.json).

**Quyết định:** giữ cơ chế selective undo, mặc định `useSelectiveEjectionUndo=true`, vì giảm rõ số bản sao snapshot và giữ nguyên hành vi trong các kiểm tra. Giữ mode full để đối chiếu; không cam kết một mức giảm wall time ổn định so với executable cũ và không cộng mức cải thiện này với phần trăm của cache P5. Chi phí snapshot/restore giảm đã được đo, nhưng tổng runtime còn bị chi phối bởi các phần ejection khác và nhiễu hệ thống.

Sau khi kết thúc phép đo, hai chương trình chính `TestALNS` và `untitled` được build Release trong `build-audit-wsl-ejection-undo/`. Không sửa cấu hình người dùng trong `TestALNS.cpp`.

## Giới hạn và tái lập

Hai seed và số vòng cấu hình 200 không đại diện cho mọi run dài hoặc toàn bộ benchmark. Ngưỡng thích ứng cũng dùng cấu hình 200, không phải prefix của run 25.000 vòng. Wall time có nhiễu hệ thống; chỉ so thời gian các run tắt profiling. Timer inclusive của snapshot/restore đã nằm trong ejection, không cộng lại vào tổng.

Với giới hạn wall time, bản chạy nhanh hơn có thể tiến được nhiều vòng hơn và cho front khác. Không tuyên bố tối ưu toàn cục hoặc thay đổi chất lượng nghiệm. Các snapshot Stage A và các Route trial còn lại chưa tối ưu ở đợt này.

Công cụ: `src/test/run_ejection_undo_audit.py`, `src/test/summarize_ejection_undo_audit.py`; xem [README_AUDIT](../src/test/README_AUDIT.md). Raw/source snapshot: `artifacts/alns_p5/ejection_undo_20261020/experiment/` (Git ignore). Kết quả rút gọn: [alns_ejection_undo_results.json](alns_ejection_undo_results.json).
