# P3–P4: charging repair cho EVRPTW-PR đối xứng

Ngày kiểm chứng: 2026-10-20. Code: commit `28a9057ed71245851249810b6ac8c2b8805628bb`, nhánh `feat/alns-charging-repair-p3-p4`.

## Kết luận

P3–P4 đã được triển khai và kiểm chứng trên đúng mô hình người dùng yêu cầu: **chỉ giao hàng, khoảng cách Euclidean đối xứng, cửa sổ thời gian, sạc một phần**. Không thêm pickup/SPD hoặc bài toán ma trận có hướng.

Ba target CTest đạt **34 ca**; toàn bộ target Release đều build thành công. Thí nghiệm đủ **18 run**, trả về **221 nghiệm / 2.792 tuyến**, tất cả qua cả validator C++ lẫn replay CSV độc lập, không sửa lịch sau khi chạy.

Charging repair cứu được move thực tế, nhưng lợi ích tổng thể chưa nhất quán: nghiệm đại diện có distance tốt hơn ở **4/9 cặp**, kém hơn ở **5/9 cặp**; số xe cuối không đổi ở cả chín cặp. HV 3D với reference chung tốt hơn ở **6/9 cặp**. **Giữ `useChargingRepairLS=false` mặc định**, cho phép bật để nghiên cứu tiếp. Ba seed chỉ là thí nghiệm thăm dò, chưa đủ khẳng định ưu thế tổng quát.

## Phần đã thay đổi

- API charging repair giữ nguyên thứ tự khách, thử cấu hình trạm cũ phù hợp và cấu hình bỏ trạm, rồi beam search quanh đoạn thiếu pin. Hỗ trợ hơn hai lượt ghé trạm và ghé lại cùng trạm. Mọi kết quả đi qua evaluator chuẩn P0–P2.
- Có giới hạn số lượt ghé trạm, số đánh giá, beam width, số trạng thái sinh và thời gian; danh sách trạm được mở rộng khi còn ngân sách. Thất bại của heuristic không phải chứng minh bất khả thi.
- Cache ứng viên trạm theo detour, lazy và giới hạn 4.096 cạnh. Các instance được kiểm tra đều có khoảng cách Euclidean đối xứng.
- Inter-relocate và Inter-2-opt được thử cứu bằng bố trí lại trạm. Chỉ commit khi cả hai tuyến khả thi và tổng khoảng cách thực giảm, đã tính detour mới. Thất bại không thay đổi nghiệm gốc. Gini/MaxTime tính lại từ lịch chuẩn.
- Thêm cờ bật/tắt, thống kê calls/evaluations/feasible/exhausted/elapsed/accepted, CSV đủ độ chính xác và cấu hình chạy. Đồng hồ vòng ALNS/thống kê chuyển sang steady_clock để tránh sai khác đồng hồ đã thấy ở run r104 dài.

## Kiểm thử và baseline

- RouteRegressionTests: 12 ca; LocalSearchRegressionTests: 8 ca; ChargingRepairTests: 14 ca. Tất cả dùng kiểm tra exception, vẫn chạy trong Release/NDEBUG.
- Fixture P3 mới hoàn toàn Euclidean đối xứng: bắt buộc ba lượt ghé trạm; trạm lặp; vận tốc/depot ready time; empty/new customer list; capacity/TW; budget/deadline; detour ranking và widening; đối chiếu vét cạn nhỏ; relocate/inter-2-opt thật được cứu; từ chối khi chi phí sau repair không giảm; rollback.
- Preflight: cùng seed 1 và 25 vòng trên c101C5/r102C10/rc108C10, mode off khớp **từng byte schedules.csv và toàn bộ objectives** với executable baseline `646ff91` đã giữ. Đây là kiểm tra xác định trên ba ca, không phải chứng minh tương đương cho mọi đầu vào.
- Run r104 dài trước P3–P4 dừng gọn ở 6.629 vòng theo yêu cầu: 52 nghiệm / 572 tuyến hợp lệ; nghiệm đại diện 11 xe, distance 1073.94204419153. Xem [báo cáo r104](r104_unlimited_run.md). Run đó có ngân sách khác nên không dùng để chấm thắng/thua trong bảng 60 giây bên dưới.

## Thiết kế đối chiếu

- WSL Ubuntu-22.04, GCC 11.4.0, CMake Release, C++17, O3/native/LTO. Cờ biên dịch, nguồn snapshot ZIP, hash executable và hash dữ liệu được giữ trong thư mục thí nghiệm.
- **Cùng một executable đã đóng băng** cho mode off/on; chạy tuần tự, đổi thứ tự mode đầu theo từng cặp. Không build hoặc chạy thêm benchmark khác trong đợt đo.
- r104_21/r101_21/c109_21 từ data/test-100: mỗi instance 100 khách, 21 trạm. Seed 1/2/3; tối đa 25.000 vòng, segment 200; 60.000 ms cho vòng ALNS; LS intensity 25; scoreNonDominated 25; Scatter Search tắt; HV stagnation limit 20. Các tham số khác giữ như baseline audit.
- Repair: 80 đánh giá, tối đa 6 lượt ghé trạm, K ban đầu 3 có mở rộng, beam 8, tối đa 3 ms/lần gọi, 8 trial mỗi lần LS.
- Cả 18 run thực tế đều dừng vì time_limit. Giới hạn vẫn mềm: khởi tạo ngoài ngân sách và LS cũ đang chạy có thể vượt mốc dừng. Ghi đầy đủ thời gian thực, không giả định mọi run chạy đúng 60.000 giây.
- Thay đổi TestALNS.cpp của người dùng (instance r103, scoreNonDominated 20) được giữ nguyên, không đưa vào commit P3–P4. Benchmark dùng ALNSAuditRunner với cấu hình cố định ở trên.

## Từng cặp seed

Nghiệm đại diện được chọn ít xe nhất rồi distance nhỏ nhất. Thắng/thua distance không đồng nghĩa toàn bộ Pareto front dominates; Gini/MaxTime có tradeoff riêng.

| Instance | Seed | Xe off/on | Distance off | Distance on | Δ distance | Gini off/on | MaxTime off/on |
|---|---:|---:|---:|---:|---:|---:|---:|
| r104_21 | 1 | 11/11 | 1113.6940 | 1087.9239 | -2.314% | 0.0461/0.0578 | 228.5717/228.7144 |
| r104_21 | 2 | 11/11 | 1106.0086 | 1095.4052 | -0.959% | 0.0622/0.0550 | 229.1189/228.3283 |
| r104_21 | 3 | 11/11 | 1092.9320 | 1128.6412 | +3.267% | 0.0634/0.0534 | 228.4465/228.5320 |
| r101_21 | 1 | 18/18 | 1646.6438 | 1631.3970 | -0.926% | 0.1040/0.1005 | 229.2194/229.2194 |
| r101_21 | 2 | 18/18 | 1629.6810 | 1647.0595 | +1.066% | 0.1078/0.0754 | 226.8851/225.7795 |
| r101_21 | 3 | 18/18 | 1654.7086 | 1642.9894 | -0.708% | 0.0991/0.1142 | 225.8769/229.2194 |
| c109_21 | 1 | 10/10 | 991.8177 | 1002.9788 | +1.125% | 0.0588/0.0709 | 1218.4415/1229.0834 |
| c109_21 | 2 | 10/10 | 986.0417 | 994.8130 | +0.890% | 0.0585/0.0541 | 1218.4415/1224.1333 |
| c109_21 | 3 | 10/10 | 991.8610 | 1000.0157 | +0.822% | 0.0528/0.0438 | 1214.6028/1218.4415 |

## HV 3D

HV dùng Distance/Gini/MaxTime với cùng reference cho mọi seed và cả hai mode của một instance. Chuẩn hóa theo chặn vật lý `(m × H × v, 1, H)`: m là số xe chung, H là dueDate depot trừ readyTime, v là vận tốc. Tổng quãng đường không vượt m×H×v với nghiệm khả thi, nên các chặn không phụ thuộc min/max riêng của từng front. Reference sau chuẩn hóa là `(1.1,1.1,1.1)`. Tất cả các cặp ở đây cùng số xe; không gộp so HV giữa các mức xe khác nhau.

| Instance | Median HV off | Median HV on | Seed HV on tốt hơn |
|---|---:|---:|---:|
| r104_21 | 0.0761687 | 0.0771134 | 2/3 |
| r101_21 | 0.0859252 | 0.0905594 | 2/3 |
| c109_21 | 0.1513778 | 0.1534124 | 2/3 |

Đây là HV 3D tính riêng để báo cáo; cơ chế dừng HV cũ của solver vẫn dùng 2D Distance/Gini. Không so sánh các giá trị HV chuẩn hóa riêng theo từng front trong log cũ. Hàm union volume được kiểm tra bằng các thể tích hộp giải tích và trường hợp điểm dominated.

## Chi phí và hoạt động repair

Các bộ đếm dưới đây là tổng ba run mode on của từng instance. Thời gian chỉ đo bên trong engine repair, không phải toàn bộ chi phí của các kiểm tra/copy/mở rộng lân cận bên ngoài engine.

| Instance | Calls | Feasible | Hết budget | Relocate nhận | Inter-2-opt nhận | Engine elapsed (s) |
|---|---:|---:|---:|---:|---:|---:|
| r104_21 | 2440 | 195 | 2245 | 43 | 0 | 3.1285 |
| r101_21 | 2950 | 83 | 2867 | 29 | 0 | 3.3355 |
| c109_21 | 4939 | 271 | 4666 | 83 | 0 | 6.6892 |

Có tổng 155 relocate được nhận qua nhánh mới; chưa quan sát Inter-2-opt được nhận qua nhánh repair trong 18 run này, dù fixture đã xác nhận nhánh đó hoạt động. Chưa có bằng chứng lợi ích riêng của Inter-2-opt trên bộ đo hiện tại.

vehiclesReduced cộng các lần loại tuyến có khách trong những nghiệm ứng viên LS, có thể xảy ra ở nhiều nhánh ALNS; **không phải số xe của nghiệm cuối giảm so với baseline**. Mức xe cuối vẫn lần lượt 11, 18, 10 cho cả hai mode.

| Instance | Median wall off/on (s) | Max vượt ngân sách search off/on (ms) |
|---|---:|---:|
| r104_21 | 60.6672/60.5870 | 561/687 |
| r101_21 | 60.4650/60.9051 | 728/821 |
| c109_21 | 60.2234/60.1961 | 288/70 |

## Cấu hình sử dụng

Giữ mặc định tắt. Thử bật bằng `config.useChargingRepairLS=true` hoặc đối số chargingRepair=1 trong ALNSAuditRunner. Không đổi chính sách VND sang scalar đa mục tiêu mới trong đợt này.

P5–P6 chưa triển khai. Nếu tiếp tục, cần profiling chi tiết để chọn chỗ cache/cập nhật gia tăng; hiện nhiều lần repair hết budget và chưa có accepted Inter-2-opt. Không tự tăng ngân sách hoặc tune theo seed vừa đo rồi coi cùng bảng này là đánh giá độc lập.

## Bằng chứng và tái hiện

- [Kết quả JSON](alns_p3_p4_results.json), [bảng từng cặp CSV](alns_p3_p4_results.csv).
- Raw: `artifacts/alns_p3_p4/paired_euclidean_60s_20261020/`: manifest, preflight, summary, source_28a9057.zip, build_environment.json, ctest_detail.log, input copies và từng run.
- Mỗi run có process.json, output/audit_parameters.json, output/results.csv, output/schedules.csv, output/run_statistics.json, output/validation.txt, independent_validation.json. Mọi validation.txt rỗng và mọi replay report có invalid=0.
- Script: `src/test/run_charging_repair_audit.py`, `validate_audit_schedule.py`, `summarize_charging_repair_audit.py`. Xem [hướng dẫn audit](../src/test/README_AUDIT.md).
- Tính lại bảng từ raw bằng `python src/test/summarize_charging_repair_audit.py artifacts/alns_p3_p4/paired_euclidean_60s_20261020 update/new_results.json` (không ghi đè file có sẵn).
- Chưa chạy nhiều seed hơn, ngân sách 180 giây hoặc bộ instance khác. Không dùng kết quả này để khẳng định chất lượng tổng quát, nghiệm tối ưu, hoặc toàn bộ bài báo sẵn sàng nộp.
