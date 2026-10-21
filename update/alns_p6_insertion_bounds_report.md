# P6 — Giới hạn thời gian trước/sau vị trí chèn

Phạm vi: EVRPTW-PR chỉ giao hàng, sạc một phần, Euclidean đối xứng. Baseline là P5 đã kiểm chứng (`aebc888`, production giống `edc8e92`), không dùng evaluator cũ có lỗi làm chuẩn.

## Thiết kế và giới hạn

Thử thuộc tính prefix/suffix tại `Route::checkInsertionCost`, dùng được ở inter-relocate và các nhánh chèn của giảm xe/ejection. Chưa thay đánh giá Inter-2-opt hoặc xây bảng mọi đoạn O(n²). Đây là phạm vi thử P6 có chủ đích: tìm lợi ích từ chặn thời gian O(1), giữ nguyên bước đánh giá lịch đầy đủ.

Cho chuỗi tuyến hiện tại, bỏ toàn bộ thời gian sạc nhưng giữ thời gian di chuyển, cửa sổ mở cửa và phục vụ khách:

- `E[i]`: thời điểm rời sớm nhất tại nút i trong relaxation này. Depot đầu xuất phát đúng giờ mở cửa; depot cuối không chờ.
- `L[i]`: giới hạn trên của thời điểm bắt đầu phục vụ tại i để còn đáp ứng các deadline trong suffix, khi thời gian sạc bằng 0. Với khách, deadline ràng buộc bắt đầu phục vụ, không ràng buộc kết thúc phục vụ.

Chèn x giữa p và j: `startX = max(readyX, E[p] + time(p,x))`; `arrivalJ = startX + serviceX + time(x,j)`. Chỉ loại nếu `startX > dueX + tolerance` hoặc `arrivalJ > L[j] + tolerance`. Trạm có service 0 trong relaxation. Tolerance tương đối `1e-7` bảo thủ hơn epsilon `1e-9` của evaluator; trường hợp không hữu hạn được chuyển sang evaluator.

Lập luận an toàn: sạc thực có thời lượng không âm, nên lịch thực không thể rời prefix sớm hơn E; bỏ thời gian sạc ở suffix chỉ nới rộng deadline cho phép. Vì thế vượt chặn đã nới thì lịch thực cũng không đáp ứng được thời gian. Không dùng giờ rời hoặc pin của lịch cũ để chặn: chèn khách có thể thay đổi lượng sạc/chờ phía trước. Thuộc tính không chứng nhận pin, không tìm lượng sạc tối ưu toàn cục và không bảo đảm nhận ra mọi phép chèn bất khả thi.

Bảng O(n) xây lười khi chèn lần đầu trên phiên bản tuyến, tra cứu O(1). Các bản copy chia sẻ bảng bất biến; thêm/xóa/đảo/clear làm mất hiệu lực trên bản sửa. Copy/move assignment mang theo bảng của chính chuỗi được gán. Dữ liệu instance phải bất biến trong lúc sử dụng Route, cùng hợp đồng với cache evaluator hiện có. Cờ CMake `ALNS_INSERTION_TIME_BOUNDS` cho phép đối chiếu bật/tắt; chế độ tắt không có bảng cache hoặc nhánh lọc trong đường chạy chèn.

## Kiểm chứng trước và sau

- Git trước sửa: `aebc888`, nhánh `feat/alns-charging-repair-p3-p4`. Giữ nguyên thay đổi người dùng ở `src/test/TestALNS.cpp` (r103, scoreNonDominated 20 khi đọc đầu đợt), dataset/log và untracked không liên quan.
- Trước sửa: 5 target CTest / 54 ca Release đạt.
- Sau sửa: cả bật và tắt P6 đều đạt 6 target / 60 ca Release bằng GCC 11.4 trong WSL Ubuntu-22.04.
- Sáu ca mới kiểm tra: deadline bắt đầu phục vụ; hai phía của phép chèn; chèn trạm cứu tuyến gốc thiếu năng lượng; mất hiệu lực sau các mutation và assignment; biên epsilon; đối chiếu ngẫu nhiên Euclidean.
- Đối chiếu ngẫu nhiên 400 instance, 17.346 phép chèn: 2.503 khả thi và 14.843 không khả thi. So với dựng chuỗi thật và evaluator chuẩn: feasibility cùng mọi delta khớp; tuyến khả thi được replay độc lập bằng C++. Có TW hẹp, giờ depot khác 0, nhiều trạm, sạc tốc độ khác nhau/kể cả 0, tải giới hạn và tuyến gốc bất khả thi.

## Thiết kế A/B

18 cặp = r104_21/r101_21/c109_21 × seed 1/2 × ba lần đo lặp, mỗi run cấu hình 200 vòng, deadline dự phòng 3.600 giây. Thêm hai warmup 25 vòng, loại khỏi số đo chính. Hai bản đều bật reconstruction cache mode 2 và selective undo; charging repair, profiling, Scatter Search tắt. Đảo thứ tự AB/BA và luân phiên instance; chạy tuần tự, không build trong lúc đo, giữ cả mẫu chậm hơn.

Baseline executable SHA-256 `a341cc95c3e27252e2e10ee4cc368de0b08949935be377af314df296d92d94a7`, đối chiếu 86 tệp production/runner với Git. ZIP nguồn trước/sau, executable, input, flags, môi trường và hash lưu trong raw. Mỗi run phải đủ vòng, C++ validation sạch và CSV replay độc lập hợp lệ; lịch/mục tiêu phải giống từng byte qua hai bản và các lần lặp. Counters reconstruction/ejection cũng phải giống nhau. Wall time dùng `time.monotonic`, bao gồm khởi tạo/solver/log/export; validator chạy sau, không tính vào runtime.

## Kết quả

Manifest đã `completed`: **36 run chính, 306 nghiệm / 3.798 tuyến đều hợp lệ**. Hai warmup thêm 8 nghiệm / 96 tuyến hợp lệ. Cả 18 cặp và các lần lặp giữ nguyên mục tiêu/lịch từng byte, cùng counters reconstruction/ejection. Không có run bị loại.

| Instance | Trung vị giảm wall time theo cặp | Cặp nhanh hơn | Tỷ lệ loại sớm lời gọi chèn |
|---|---:|---:|---:|
| r104_21 | 2,03% | 5/6 | 92,94% |
| r101_21 | 2,72% | 5/6 | 97,95% |
| c109_21 | 5,22% | 4/6 | 83,62% |

Toàn bộ 18 cặp: **trung vị giảm wall time 2,72%**, 14 cặp nhanh hơn, 4 cặp chậm hơn; khoảng **−2,03% đến +7,57%**. Tổng baseline **243,290s** xuống **236,318s**, giảm tổng **2,87%**, tiết kiệm 6,972s. CPU time có trung vị giảm 2,92%, chỉ là số đo bổ sung trong WSL, không dùng suy luận thời gian chờ.

291.282.675 lời gọi chèn ở các run P6 chính, 264.723.915 bị loại sớm, 15.250.458 lần dựng bảng. Các tổng này gồm ba lần lặp đo, không phải số đầu vào/seed độc lập. Tỷ lệ loại cao không đồng nghĩa giảm runtime tương ứng: vẫn có chi phí dựng bảng, copy tuyến và những nhánh evaluator/reconstruction không đi qua phép chèn này.

Ở c109 seed 1, trung vị theo cặp là −0,45%, còn tỷ lệ từ hai trung vị thời gian gần như bằng nhau (+0,008%). Không khẳng định mọi instance/seed đều nhanh hơn. Đây là lợi ích nhỏ trên cấu hình đã đo, chưa phải chứng nhận tốc độ cho run dài hoặc kết quả thống kê tổng quát.

## Quyết định và phần tiếp theo

Giữ bộ lọc và đặt CMake default ON: số đo tổng và trung vị trên từng instance đều thuận lợi, giới hạn chậm hơn trong đợt đo khoảng 2%, kiểm chứng quyết định/lịch đạt. OFF vẫn loại hoàn toàn cache/nhánh lọc khỏi đường chạy chèn để đối chiếu hoặc quay về baseline. Chưa bật charging repair; các tham số/neighborhood/ngân sách và chính sách sạc giữ nguyên.

Sau khi đo xong chỉ đổi default/comment của CMake từ OFF sang ON và cập nhật tài liệu; đợt A/B vốn đã biên dịch bằng `-DALNS_INSERTION_TIME_BOUNDS=ON`, nên code thuật toán được đo và bản default mới là cùng một đường chạy. ZIP raw giữ nguyên trạng thái trước khi chốt default. SHA-256 executable P6: `420016ebbd7dda5744da6b5ccee941fd3a770831a1484d1623f0c64709cddd25`.

Không cộng 2,72% với mức khoảng 27% của đợt trước: hai thí nghiệm có baseline và thời điểm đo khác nhau. P6 đã hoàn tất thử nghiệm giới hạn cho insertion prefix/suffix; chưa triển khai thuộc tính đoạn tổng quát cho Inter-2-opt. **P7 benchmark dài hơn/thêm seed vẫn còn**, cần kiểm tra lợi ích ở cấu hình chạy thực trước khi chốt tuyên bố hiệu năng cuối. Cache cặp tuyến và sửa topology là các ý tưởng P5 còn hoãn, không gộp vào thay đổi này.

Kết quả máy đọc: `update/alns_p6_insertion_bounds_results.json`.

Raw: `artifacts/alns_p6/insertion_bounds_20261021/experiment/`. Công cụ: `src/test/run_insertion_bounds_audit.py`, `src/test/summarize_insertion_bounds_audit.py`.
