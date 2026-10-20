# ALNS P0-P2: evaluator, local search và kiểm chứng trước/sau

Mô hình đích: EVRPTW-PR, chỉ giao hàng và sạc một phần. Các sửa đổi không thêm pickup, biến tải nhận hàng hoặc ràng buộc simultaneous pickup-delivery. HMA-SPD chỉ là nguồn tham khảo kỹ thuật tìm kiếm và bố trí trạm.

## Mốc và môi trường

- Commit gốc trước đợt làm việc: `7cc90ac`, branch ban đầu `test01`.
- Branch thực hiện: `fix/alns-evaluation-p0-p2`.
- Workspace ban đầu có thay đổi chưa commit trong CMake, Instance và TestALNS, cùng runner JD. Snapshot riêng lưu các thay đổi này trước các commit sửa lỗi; không dùng reset/stash để xóa working tree.
- Compiler: MinGW GCC 13.2.0, C++17, CMake `RelWithDebInfo`, cùng tùy chọn build cho hai bản.
- Bản trước sửa có executable riêng tại `artifacts/alns_p0_p2/before/ALNSAuditRunner.exe`. Source gốc của các file liên quan, diff, trạng thái Git và SHA256 lưu trong `artifacts/alns_p0_p2/original/`.
- Phạm vi hoàn tất là P0-P2. P3-P7 của kế hoạch vẫn là các bước tiếp theo; chưa bổ sung HMA charging repair chung, cache lân cận hoặc thuộc tính đoạn.

## Các thay đổi

1. `Customer` khởi tạo đầy đủ dữ liệu thời gian của `Node`. Trước sửa, các toán tử đọc qua `Node*` có thể đọc trường chưa khởi tạo dù các getter riêng của `Customer` đúng.
2. Đánh giá tuyến và phép chèn ảo dùng chung một evaluator, gồm chính sách sạc xác định. Getter không còn thực hiện một lượt absorption khác làm thay đổi trạng thái và active time.
3. Lượng sạc thêm chỉ dùng thời gian chờ có thể hấp thụ, bị giới hạn bởi slack của mọi khách trung gian, dung lượng pin và giờ đóng trạm. Forward pass lan truyền pin đầy đủ, đồng thời tính lại sạc tại các trạm sau. Hỗ trợ rate khác nhau và thời gian di chuyển độc lập với khoảng cách.
4. Bộ lọc chèn dùng tổng nhu cầu và chặn vật lý; không loại ứng viên chỉ vì lượng pin hoặc giờ rời trong lịch cũ. Intra move có tính phần khách bị gỡ. Delta chèn khớp kết quả dựng tuyến thật, kể cả Wait/Charge/Time.
5. Inter-relocate kiểm tra tuyến nguồn sau khi gỡ khách. Delta intra-relocate sửa ánh xạ cạnh chèn. Directed 2-opt và reversed Or-opt tính thêm các cạnh nội bộ đảo chiều.
6. Nhánh relocation có thêm trạm chỉ áp dụng cho hai tuyến khác nhau và phải giảm tổng khoảng cách thực sau bổ sung trạm.
7. Tham số độ sâu ejection được truyền vào chuỗi đệ quy thực, tối đa năm lần đẩy khách; có giới hạn 2.000 nhánh thử/victim và snapshot để hoàn tác. Các block cũ có tên depth-4/depth-5 nhưng lặp thao tác độ sâu thấp đã được thay thế. Đây là thay đổi heuristic tìm kiếm, không phải tối ưu giữ nguyên đường đi tìm kiếm.
8. Stage regret của ejection lưu và áp dụng đúng trạm đã dùng khi chấm ứng viên; trước đó có nhánh chấm cấu hình có trạm nhưng commit phép chèn trực tiếp.
9. Xóa trạm kiểm tra lại sau từng lần xóa vì khả năng dư trạm phụ thuộc lượng sạc ở các trạm khác. Time slack của khách dùng deadline bắt đầu phục vụ, không trừ nhầm thời gian phục vụ cố định.
10. Xác suất LS không còn khoảng trống 22%-25%, được chặn trong [0,100]. Thống kê ghi số vòng thực chạy, lý do dừng, thời gian khởi tạo, vòng tìm kiếm và tổng thời gian. Khi dừng do HV, nghiệm mượn từ pool được trả lại.
11. Bổ sung hai target CTest, validator replay lịch độc lập và runner audit. Sửa hai lỗi build có sẵn của TuningApp: thiếu `<algorithm>` và tên API workload cũ; giữ format nhãn output của runner.

## Kiểm thử

- Trước sửa: 9/9 ca hồi quy đầu tiên thất bại. Log: `artifacts/alns_p0_p2/before/route-regression.txt`.
- Sau sửa: `RouteRegressionTests` đạt 12 ca; `LocalSearchRegressionTests` đạt 8 ca. CTest đạt 2/2 target.
- Có đối chiếu 100 bộ ma trận directed nhỏ với mọi vị trí chèn được xét; 20 bộ directed cho ejection ở các mức 0/1/3/5; kiểm tra coverage và hoàn tác.
- Validator kiểm tra lịch xuất ra theo từng cạnh: arrival/departure/wait, pin đến/rời, lượng sạc, đóng trạm, tải và customer coverage; tính lại Distance, Gini và MaxTime. Dữ liệu khách được đọc độc lập qua Customer để tránh phụ thuộc lỗi lớp Node cũ.
- Build toàn bộ target hiện có trong workspace thành công: main, TestALNS, RunJD, TuningApp, các benchmark và các target audit.

Các test xác nhận tính đúng cho các trường hợp đã chạy, không chứng minh chính sách sạc tối ưu toàn cục hoặc tìm kiếm bao phủ mọi nghiệm khả thi.

## Đối chiếu cùng ngân sách

Chạy tuần tự, seed 1 và 2, cùng cấu hình: tối đa 25.000 vòng, 10.000 ms cho vòng ALNS, segment 200, LS intensity 25, Scatter Search tắt. Giữ các tham số khác như nhau. Khi HV dừng sớm thì không ép tiếp tục chạy đủ 10 giây; báo thời gian và lý do dừng.

| Instance | Seed | Xe trước | Xe sau | Distance trước | Distance sau | Nghiệm bị validator báo lỗi trước | Sau |
|---|---:|---:|---:|---:|---:|---:|---:|
| c101C5 | 1 | 2 | 2 | 257.747452 | 257.747452 | 11/11 | 0/11 |
| c101C5 | 2 | 2 | 2 | 257.747452 | 257.747452 | 10/10 | 0/11 |
| r102C10 | 1 | 4 | 3 | 262.924769 | 249.189320 | 13/13 | 0/12 |
| r102C10 | 2 | 4 | 3 | 262.924769 | 249.189320 | 9/9 | 0/9 |
| rc108C10 | 1 | 3 | 3 | 345.927320 | 345.927320 | 9/27 | 0/11 |
| rc108C10 | 2 | 3 | 3 | 345.927320 | 345.927320 | 8/24 | 0/9 |
| r101_21 (100 khách) | 1 | 20 | 20 | 1720.420592 | 1721.037367 | 5/5 | 0/2 |

Nghiệm đại diện trong bảng chọn ít xe nhất rồi Distance nhỏ nhất. Bản cũ có lỗi lịch/chỉ số và dữ liệu Node chưa khởi tạo, do đó các chênh lệch trên chỉ là quan sát trước/sau toàn bộ gói sửa; không dùng chúng để kết luận riêng ejection hay HMA làm tăng chất lượng bao nhiêu.

Sáu run nhỏ của bản cuối có 63 nghiệm, đều qua validator. Run 100 khách bổ sung có hai nghiệm, đều qua validator. Trước sửa có 65 nghiệm bị báo lỗi trên tổng 99 nghiệm của bảy run.

Ở run 100 khách, khoảng cách đại diện tăng khoảng 0,036% và thời gian tổng khoảng 11,46 giây so với 11,04 giây trước sửa. Chưa có bằng chứng tăng tốc hoặc cải thiện chất lượng tổng quát. Cần dùng bản đã sửa làm baseline cho P3-P7 và thử thêm seed/ngân sách lớn hơn.

## Cách chạy lại

```powershell
cmake -S . -B build-audit-after -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=RelWithDebInfo "-DCMAKE_CXX_COMPILER=C:/Strawberry/c/bin/g++.exe" "-DCMAKE_MAKE_PROGRAM=C:/Strawberry/c/bin/gmake.exe"
cmake --build build-audit-after -j 4
ctest --test-dir build-audit-after --output-on-failure
python -u src/test/run_alns_audit.py build-audit-after/ALNSAuditRunner.exe artifacts/alns_p0_p2/new_run 10
```

Runner từ chối ghi đè thư mục run đã tồn tại. Kết quả đầy đủ của bản cuối ở `artifacts/alns_p0_p2/after/final_runs/`; run lớn ở `after/medium_r101_seed1/`. Summary JSON gọn được lưu cùng báo cáo trong `update/alns_p0_p2_results.json`.

## Các giới hạn còn lại

- Giới hạn thời gian hiện là giới hạn mềm cho vòng ALNS: khởi tạo nằm ngoài ngân sách và một lần LS đang chạy có thể vượt mốc dừng. Đã ghi đầy đủ wall time; deadline bên trong tìm kiếm dài cần làm ở bước ngân sách repair tiếp theo.
- KNN, top-K, centroid và các ngưỡng khoảng cách vẫn là giới hạn heuristic. Bộ lọc chèn an toàn hơn không có nghĩa toàn bộ local search đã đầy đủ với mọi ma trận directed.
- Điều kiện dừng HV hiện vẫn dùng 2D Distance/Gini. Đánh giá nâng cấp đa mục tiêu về sau phải dùng cùng reference box và ghi riêng HV 3D.
- Chưa chạy batch JD200 hoặc đối chiếu chất lượng với HMA sau khi thống nhất nhu cầu, ma trận Floyd/hyperarc và hàm mục tiêu.
- Chính sách sạc là heuristic khả thi, xác định; với rate khác nhau nó chưa bảo đảm tìm được mọi lịch sạc khả thi cho một thứ tự node.

## Bước tiếp theo

Triển khai P3: charging repair giữ nguyên thứ tự khách, có ngân sách và trạng thái thất bại rõ ràng. Sau đó tích hợp P4 vào Inter-relocate và Inter-2-opt với cờ bật/tắt để đo riêng lợi ích so với baseline đã sửa.
