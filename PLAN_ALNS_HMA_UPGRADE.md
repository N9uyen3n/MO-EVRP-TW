
# Kế hoạch nâng cấp ALNS từ việc đối chiếu HMA

Ngày lập: 2026-10-20. Cập nhật: P0–P4 đã triển khai và kiểm chứng; P3–P4 giữ dạng tùy chọn, mặc định tắt vì lợi ích phụ thuộc seed/mục tiêu. P5 đã có profiling và cache thất bại của helper tái dựng, bật mặc định trên solver sau kiểm chứng. Cache cặp tuyến/cập nhật lân cận tổng quát chưa triển khai; P6–P7 chưa bắt đầu. Xem [báo cáo P0-P2](update/alns_p0_p2_report.md), [báo cáo P3–P4](update/alns_p3_p4_report.md), [profiling P5](update/alns_p5_profiling_report.md), [cache P5](update/alns_p5_cache_report.md) và [run r104 đã kiểm chứng](update/r104_unlimited_run.md).

## 1. Mục tiêu và phạm vi

Mô hình đích là EVRPTW-PR: chỉ giao hàng, có cửa sổ thời gian và sạc một phần. Mỗi khách có một demand giao; tổng demand trên tuyến không vượt tải trọng xe. Không bổ sung pickup hoặc simultaneous pickup-delivery.

Phạm vi được người dùng chốt lại: dữ liệu Solomon với khoảng cách Euclidean đối xứng, thời gian di chuyển theo vận tốc của instance. P3–P4 và benchmark tập trung đúng mô hình này; không phát triển thêm yêu cầu ma trận có hướng hoặc mở rộng JD/SPD. Những kiểm thử directed đã làm trong P0–P2 là lịch sử kiểm chứng triển khai, không phải yêu cầu của bài toán đích.

Nâng cấp local search để đánh giá nghiệm nhất quán, giảm việc bỏ sót move khả thi do năng lượng, sau đó tăng tốc các bước tìm kiếm lặp lại. Giữ thứ tự mục tiêu hiện tại: ít xe hơn được ưu tiên tuyệt đối; khi bằng số xe, xét Pareto trên Distance, Gini của active time và MaxTime.

Nguồn tham khảo là `../EVRP_SPD/EVRP-TW-SPD-HMA/src`. Học cơ chế tính sạc có xét slack trung gian, phục hồi trạm sau move, thuộc tính đoạn và cập nhật lân cận bị ảnh hưởng. Mọi cơ chế được chuyển sang phải phù hợp với mô hình, ma trận và hàm mục tiêu của ALNS.

Phạm vi đợt đầu là evaluator, bộ lọc chèn, local search và công cụ kiểm chứng cho mô hình chỉ giao hàng. HMA-SPD chỉ cung cấp ý tưởng tìm kiếm, tính sạc và cache. Thay ALNS bằng HMA, genetic station insertion và phân rã bài toán thành quần thể cần đánh giá riêng; mở rộng sang pickup-delivery nằm ngoài kế hoạch này.

## 2. Căn cứ đã xác nhận

| Phát hiện | Bằng chứng | Ảnh hưởng |
|---|---|---|
| Dữ liệu thời gian của Customer và Node không đồng nhất | Phát hiện và thêm regression khi triển khai P0; Customer chưa khởi tạo các trường thời gian của Node | Đọc qua Node* có thể dùng giá trị chưa khởi tạo; đã sửa trong P1 |
| Bộ lọc pin theo trạng thái cũ loại phép chèn khả thi | Ví dụ nhỏ: `canPossiblyInsert=false`, `checkInsertionCost=true`, tuyến thực sau chèn khả thi | Bỏ sót ứng viên trước khi thử phục hồi năng lượng |
| Slack absorption có thể làm khách trung gian trễ hạn | Ví dụ nhỏ: sau `getStates()`, A đến lúc 21, deadline 12, `isFeasible=true` | Lịch trả về và cờ khả thi không nhất quán |
| Getter có thể thay đổi active time | Cùng tuyến: active time từ 24 thành 34 sau `getStates()` | Gini có thể phụ thuộc thứ tự truy cập |
| Pin sau absorption chưa được lan truyền đầy đủ | Pin tại depot 76, nhưng tính từ trạng thái trước đó phải là 86 | Energy slack và các bước lọc dựa trên trạng thái có thể sai |
| Hàm tái dựng chưa phải charging repair chung | Từ chối danh sách khách mới rỗng; chỉ thử tối đa hai trạm; có bước dùng distance làm time | Chưa thể đưa trực tiếp vào mọi move |
| HMA thay ma trận trước khi giải | `Data::floydWarshall()` chọn đường nhanh qua trạm, cập nhật time, distance và hyperarc | Chưa thể so kết quả trực tiếp với JD dùng ma trận gốc |

Hai ví dụ đầu đã được biên dịch và chạy trên các file `src/core/*.cpp` hiện tại. Khi triển khai, đưa chúng vào test lâu dài; không phụ thuộc file thử trong thư mục tạm.

## 3. Thứ tự và các mốc bàn giao

```text
P0: Chốt baseline và kiểm chứng
  -> P1: Sửa evaluator và lịch sạc
  -> P2: Sửa bộ lọc và đánh giá move
  -> P3: Tách charging repair giữ thứ tự khách
  -> P4: Tích hợp repair vào local search
  -> P5: Cache và cập nhật lân cận
  -> P6: Thử thuộc tính đoạn
  -> P7: Benchmark tổng hợp và chọn cấu hình mặc định
```

Không lấy kết quả của evaluator cũ làm chuẩn tính đúng. Lưu kết quả cũ để truy vết; từ P1-P2, dùng bản đã sửa làm baseline cho các phép đo nâng cấp tìm kiếm. Mỗi bước có thay đổi độc lập và báo cáo riêng.

## P0. Chốt baseline, dữ liệu và công cụ đo

**Công việc**

- Ghi nhận trạng thái mã nguồn, compiler, build type, seed, cấu hình ALNS và đường dẫn dữ liệu. Bảo toàn các thay đổi sẵn có trong workspace.
- Lưu kết quả baseline vào thư mục mới theo mã đợt chạy; không ghi đè log cũ.
- Thêm target kiểm thử hồi quy riêng, dự kiến `RouteRegressionTests`, liên kết `alns_lib`; đăng ký với CTest.
- Thêm bộ kiểm chứng lịch độc lập: nhận node sequence, thời gian đến/rời và lượng sạc; kiểm tra từng cạnh, pin, tải, cửa sổ thời gian và customer coverage.
- Kiểm chứng cả lịch xuất ra và chỉ số Distance/Gini/MaxTime. Validator JD hiện chỉ dựng lại lịch sạc tối thiểu nên chưa phát hiện hết lỗi trạng thái do absorption.
- Đo thời gian tổng gồm khởi tạo, local search và Scatter Search nếu bật; ghi riêng thời gian vòng ALNS. Ghi số vòng thực chạy và lý do dừng.

**File dự kiến:** `CMakeLists.txt`, `src/test/RouteRegressionTests.cpp`, công cụ validator/benchmark trong `src/test/`, logger và runner nếu cần thêm trường đo.

**Hoàn thành khi:** có thể tái chạy một trường hợp bằng cấu hình đã lưu; hai lỗi nhỏ được thể hiện bằng kiểm thử xác định; bộ kiểm chứng độc lập phát hiện lịch sai dù evaluator báo khả thi.

## P1. Sửa tính nhất quán của evaluator và lịch sạc

**Công việc**

- Chốt một chính sách lịch sạc xác định cho mỗi node sequence. Chính sách phải cho cùng kết quả bất kể thứ tự gọi getter.
- Gom tính lịch, mức pin, thời gian chờ, lượng sạc và tổng chỉ số vào một quá trình chuẩn. Có thể giữ lazy evaluation, nhưng mọi getter phải đọc cùng trạng thái đã hoàn tất.
- Sửa absorption: lượng sạc thêm bị chặn bởi dung lượng pin, giờ đóng trạm và slack nhỏ nhất của mọi khách bị trì hoãn, không chỉ thời gian chờ ở khách đích.
- Khi sạc thay đổi, lan truyền pin và thời gian xuống tuyến; tính lại lượng sạc cần ở trạm tiếp theo theo cùng chính sách. Tránh chỉ sửa một phần `states`.
- Không mặc định station charging rate giống nhau; kiểm tra đơn vị time/energy và trường hợp tốc độ sạc khác nhau.
- Đồng bộ logic giữa `evaluate()`, `checkInsertionCost()` và các kết quả delta. Không tuyên bố tối ưu lượng sạc toàn cục nếu chỉ dùng heuristic xác định.
- Sau một lần sửa sequence, vô hiệu hóa đầy đủ trạng thái liên quan. `Solution::evaluateRoutes()` phải tổng hợp từ lịch chuẩn của tất cả tuyến.

**File chính:** `include/core/Route.h`, `src/core/Route.cpp`, `src/core/Solution.cpp`.

**Kiểm chứng bắt buộc**

- Hai lỗi absorption đã tái hiện: vi phạm deadline trung gian và pin không bảo toàn.
- Cùng tuyến, gọi các getter theo nhiều thứ tự: Distance, Wait, Charge, ActiveTime, States và Gini không đổi.
- Có hai hoặc nhiều trạm; khách có TW chặt trước một khách chờ dài; station TW chặt; rate khác nhau; depot mở cửa khác 0; không có trạm; tuyến rỗng.
- Kiểm tra cân bằng pin trên từng cạnh và tính lại chỉ số từ lịch xuất ra bằng validator độc lập.

**Hoàn thành khi:** mọi lịch báo khả thi vượt kiểm chứng độc lập; các getter không làm thay đổi ý nghĩa nghiệm; regression tests đạt. Chưa chuyển sang tăng tốc trước mốc này.

## P2. Sửa bộ lọc nhanh và đánh giá move

**Công việc**

- Phân biệt rõ: điều kiện loại chắc chắn, ước lượng để xếp hạng và kiểm tra đầy đủ. Chỉ điều kiện cần đã chứng minh mới được dùng để loại chắc chắn.
- Thay kiểm tra pin dựa trên lịch cũ bằng chặn an toàn hoặc hoãn kiểm tra năng lượng đến evaluator. Phải tính đến khả năng tăng lượng sạc ở trạm trước vị trí chèn.
- Rà tải trọng của intra-relocate/swap: khi xét phép chuyển trong cùng tuyến phải tính khách bị gỡ, tránh coi là chèn thêm một bản sao.
- Rà lại ánh xạ chỉ số và delta của intra-relocate trước/sau khi xóa node.
- Inter-relocate kiểm tra cả tuyến nguồn và tuyến đích trước commit; không dựa duy nhất vào tính khả thi của phép chèn vào tuyến đích.
- Rà các bộ lọc 2-opt và reversed Or-opt trên directed matrix. Khi đảo đoạn, tính cả chi phí cạnh nội bộ bị đảo hoặc bỏ bộ lọc chưa an toàn.
- Sửa khoảng tiến độ 22%-25% khiến `lsIntensity=0` ngoài ý định; làm rõ và thực thi tham số độ sâu ejection trước khi đo chính sách tăng cường.
- Giữ các bộ lọc KNN/top-K dưới tên heuristic giới hạn tìm kiếm; không diễn giải chúng là chứng minh lân cận đầy đủ.

**File chính:** `src/core/Route.cpp`, `src/alns/LocalSearch.cpp`, `include/alns/LocalSearch.h`, `src/alns/ALNSSolver.cpp`.

**Kiểm chứng:** ví dụ `quick=false/exact=true` phải qua được chặn an toàn mới; kiểm tra đối chiếu filter với evaluator trên nhiều tuyến nhỏ xác định; kiểm tra delta dự đoán so với dựng tuyến thực cho intra/inter và ma trận bất đối xứng.

**Hoàn thành khi:** các chặn được coi là an toàn không loại trường hợp evaluator đầy đủ chấp nhận trong bộ đối chiếu; move commit giữ đúng customer coverage; có baseline đã sửa để dùng ở P3-P7.

## P3. Tách bộ phục hồi trạm giữ nguyên thứ tự khách

**API dự kiến**

```cpp
ChargingRepairResult repairChargingForCustomerSequence(
    const Route& original,
    const std::shared_ptr<Instance>& instance,
    const std::vector<int>& orderedCustomers,
    const ChargingRepairBudget& budget);
```

`orderedCustomers` chứa đúng thứ tự cần phục vụ, không gồm depot/trạm. Kết quả chứa tuyến thử và trạng thái success, budget exhausted hoặc no candidate found. Thất bại của heuristic không được hiểu là chứng minh không tồn tại cách chèn trạm.

**Công việc**

- Tách việc sắp khách khỏi việc chọn trạm. Hỗ trợ bố trí lại trạm cho tuyến không có khách mới, cũng như tuyến cần nhiều hơn hai trạm.
- Dùng `getTime()` cho thời gian, `getDistance()` cho khoảng cách/năng lượng; bắt đầu từ ready time của depot.
- Thử cấu hình trạm hiện có, sau đó chèn tuần tự quanh đoạn thiếu pin; nếu cần, chạy tìm kiếm có giới hạn bằng beam hoặc nhãn trạng thái.
- Ngân sách phải giới hạn số ứng viên/trạng thái và số trạm; có deadline để local search không chạy quá lâu trong một lần gọi.
- Mọi tuyến đầu ra đi qua evaluator chuẩn P1 và kiểm tra đúng tập khách. Hàm không sửa nghiệm gốc nếu thất bại.
- Tạo cache ứng viên trạm trong `Instance`, trả top-K theo detour Euclidean; dùng chiến lược lazy/capped để tránh bảng O(N²S) không cần thiết.
- Top-K là giới hạn heuristic, có cơ chế mở rộng khi ngân sách cho phép. Đừng chỉ chọn trạm gần khách theo Euclidean.

**File dự kiến:** `include/alns/ChargingRepair.h`, `src/alns/ChargingRepair.cpp`, `include/core/Instance.h`, `src/core/Instance.cpp`; chuyển logic dùng chung từ local search và các repair hiện có theo từng bước.

**Kiểm chứng:** giữ thứ tự khách; không sửa đầu vào khi fail; zero/new/multiple station visits; ba lượt ghé trạm bắt buộc trong ví dụ Euclidean nhỏ; thời gian di chuyển theo vận tốc; giới hạn ngân sách. Với ví dụ rất nhỏ, so tập ứng viên với vét cạn cấu hình trạm giới hạn.

**Hoàn thành khi:** có API dùng chung, kết quả và ngân sách rõ ràng; hỗ trợ được ví dụ cấu hình trạm cũ thất bại nhưng bố trí khác thành công.

## P4. Tích hợp charging repair vào local search theo cách CLS

**Công việc**

- Thêm cờ thử nghiệm `useChargingRepairLS`, tắt trong baseline đã sửa.
- Tích hợp Inter-relocate trước, Inter-2-opt sau. Chỉ sửa các tuyến bị ảnh hưởng.
- Luồng ứng viên: lọc an toàn -> đánh giá cấu hình hiện tại -> nếu có triển vọng, thử phục hồi trạm trong ngân sách -> đánh giá lại chi phí thực -> commit nguyên tử.
- Tính cả detour và thời gian sạc mới sau repair; move giảm khoảng cách sơ bộ không đủ điều kiện để được chấp nhận.
- Pha distance chỉ chấp nhận giảm khoảng cách thực sau repair. Pha giảm xe chấp nhận ít xe hơn theo ưu tiên hiện hành. Nghiệm không dominated về ba mục tiêu tiếp tục do tầng ALNS/archive xử lý.
- Gini/MaxTime được tính lại từ lịch chuẩn. Chưa thay tiêu chí VND thành một scalar đa mục tiêu mới trong cùng thay đổi này.
- Không để nhánh `continue` hoặc bộ lọc pin cũ chặn toàn bộ đường đến repair.
- Ghi số lần thử repair, thành công khả thi, thành công được chấp nhận, số xe giảm và thời gian tiêu tốn; phân biệt hết ngân sách với không tìm thấy cấu hình.

**File chính:** `include/alns/ALNSSolver.h`, `include/alns/LocalSearch.h`, `src/alns/ALNSSolver.cpp`, `src/alns/LocalSearch.cpp`, logger.

**Hoàn thành khi:** có ví dụ move được cứu bằng đổi trạm; repair không còn lợi sau tính chi phí thực thì không được commit; mọi nghiệm cuối đạt validator; benchmark ghép cặp với baseline đã sửa cho thấy có ích trước khi bật mặc định.

## P5. Cache lân cận và cập nhật gia tăng

**Trạng thái:** profiling xác định phần giảm xe tốn thời gian nhất, sau đó đo riêng helper tái dựng và triển khai cache thất bại có giới hạn. 46 regression đạt. Đợt cache có 21 run trên r104/r101/c109, hai seed, 200 vòng; 168 nghiệm / 2.070 tuyến hợp lệ, sáu bộ trước/tắt/bật có mục tiêu và lịch giống nhau. Cache tránh 290.312 phép tính; thời gian giảm 7,93–20,11% so bật/tắt cùng executable, trung vị 11,93%. Bật mặc định qua ALNSConfig, giữ Off/Observe để đối chiếu. Chi tiết: [profiling](update/alns_p5_profiling_report.md), [cache](update/alns_p5_cache_report.md).

**Điều chỉnh theo số đo:** dùng full sequence/thứ tự khách/thông số xe/giới hạn trạm làm khóa helper xác định, không cần dựa vào route identity/version hay ngữ cảnh mục tiêu. Cache chỉ tồn tại trong một entry point LS, tối đa 4.096 mục; dữ liệu instance cố định trong session. Đây là tối ưu cục bộ đã kiểm chứng, không triển khai cache thất bại của cả toán tử VND/ejection. Các việc về identity/version và top-8 bên dưới dành cho mở rộng cache cặp tuyến trong tương lai; chưa thay đổi phạm vi lân cận ở mốc này.

**Công việc**

- Đo profiling trước; chọn toán tử có nhiều lần đánh giá lại để làm đầu tiên.
- Dùng route identity ổn định và version khi sequence thay đổi. Không dựa vào `Route::getId()` hiện tại nếu chưa bảo đảm duy nhất, vì một số hàm tái dựng gán ID trùng.
- Bắt đầu bằng cache kết quả không cải thiện trong một pha có phạm vi tìm kiếm cố định.
- Khóa cache gồm toán tử, hai identity/version, hướng chuyển và cấu hình phạm vi tìm kiếm. Khi mở `maxNodesToCheck_`, `maxSwapAttempts_`, top-K hoặc đổi chế độ mục tiêu, phải làm hết hiệu lực mục tương ứng.
- Chỉ tính lại các cặp bị ảnh hưởng; xử lý xóa/chèn/tái dựng tuyến và chuyển sang nghiệm ALNS khác.
- Kiểm tra cập nhật top-8 tuyến lân cận: tuyến vừa đổi centroid có thể trở thành hàng xóm mới dù không nằm trong danh sách cũ.
- Nếu sau này chấm theo Gini/MaxTime toàn nghiệm, thêm ngữ cảnh mục tiêu vào điều kiện hết hiệu lực cache.

**File chính:** `include/core/Route.h`, `include/alns/LocalSearch.h`, `src/alns/LocalSearch.cpp`.

**Hoàn thành khi:** bật/tắt cache trong cùng cấu hình tìm kiếm xác định cho kết quả move nhất quán; cache không dùng lại kết quả khi mở rộng phạm vi; có số đo cache hit và thời gian giảm. Tối ưu cache không được dựa vào thay đổi lân cận để tạo lợi thế hiệu năng.

## P6. Thử thuộc tính đoạn để giảm copy/evaluate

**Công việc**

- Thử prefix/suffix cho Inter-relocate và Inter-2-opt trước; giữ cờ tắt/bật để đối chiếu.
- Lưu distance, customer demand và thuộc tính TW phù hợp với chuỗi khách; ghép một số đoạn cố định để lọc ứng viên.
- Xác định rõ ràng thuộc tính là chính xác trong miền nào. Thuộc tính bỏ trạm không phải chứng nhận khả thi pin hoặc thời gian sạc.
- Chỉ xây bảng mọi đoạn O(n²) khi profiling cho thấy thời gian tiết kiệm vượt chi phí rebuild và bộ nhớ.
- Giữ evaluator chuẩn và charging repair làm bước kiểm tra cuối.

**Hoàn thành khi:** thuộc tính đoạn khớp cách dựng chuỗi thực trên bộ đối chiếu, không làm sai quyết định feasibility, và có lợi ích thời gian đo được. Nếu không có lợi, giữ bản đơn giản đã đạt P5.

## P7. Benchmark tổng hợp và chọn cấu hình mặc định

**Thiết kế thí nghiệm**

- Bộ nhỏ: chọn đại diện C/R/RC có 5, 10, 15 khách; dùng cho regression và đối chiếu move.
- Bộ vừa: chọn đại diện C/R/RC trong `data/test-100` và các instance đã dùng trước đó; chốt manifest đường dẫn thực tế trước khi chạy.
- Nếu mở rộng kích thước sau này, tiếp tục dùng dữ liệu Euclidean đối xứng của cùng mô hình EVRPTW-PR; đợt này chỉ benchmark ba instance 100 khách đã chọn.
- Smoke test ban đầu: ba instance, hai seed, 10 giây mỗi run. Đánh giá chất lượng: năm seed ghép cặp, 60 giây; xác nhận trên tập chọn trước ở 180 giây khi cần.
- Chạy tuần tự trên cùng máy/build; ghi wall time tổng, thời gian riêng ALNS, mức vượt deadline và lý do dừng. Kiểm tra deadline bên trong các tìm kiếm tốn thời gian; đặt giới hạn vòng đủ lớn và ghi rõ run nào dừng do vòng/HV thay vì thời gian.
- Dùng thư mục kết quả mới cho mỗi biến thể. Lưu baseline cũ để truy vết, baseline P1-P2 để đánh giá nâng cấp.
- So từng biến thể riêng: baseline đã sửa; +charging repair; +cache; +thuộc tính đoạn; cấu hình kết hợp. Giữ các tham số khác cố định trong mỗi phép so.

**Chỉ số báo cáo**

| Nhóm | Chỉ số |
|---|---|
| Tính đúng | Vi phạm lịch, pin, tải, coverage; chênh chỉ số báo cáo với validator |
| Mục tiêu ưu tiên | Số xe tốt nhất, trung vị và tần suất đạt mức xe thấp nhất qua các seed |
| Chất lượng khi cùng số xe | Distance, Gini, MaxTime; nghiệm đại diện ít xe nhất rồi distance nhỏ nhất |
| Đa mục tiêu | HV 3D với cùng reference box cho các biến thể trên cùng instance và cùng mức xe; ghi riêng HV 2D nếu giữ điều kiện dừng cũ |
| Chi phí | Wall time, số move đánh giá, số lần full evaluate/repair, thời gian từng toán tử, cache hit, bộ nhớ nếu dùng bảng đoạn |

HMA chỉ là nguồn tham khảo cơ chế. Đợt này so sánh bật/tắt charging repair trong ALNS trên cùng dữ liệu Solomon đối xứng, không thực hiện so sánh chất lượng với HMA-SPD.

**Điều kiện bật mặc định**

- Tất cả regression và validator đạt; không chấp nhận đổi chất lượng để che lỗi feasibility.
- Bản nâng cấp tìm kiếm có lợi ích lặp lại trên nhiều seed trong cùng ngân sách thời gian; báo cáo đầy đủ trường hợp tốt hơn và xấu hơn.
- Bản chỉ tăng tốc giữ ngữ nghĩa tìm kiếm và giảm thời gian đo được; nếu chi phí cache/rebuild vượt lợi ích thì để tắt.
- Giữ các cờ thử nghiệm để tháo riêng từng cơ chế; không đặt lại lỗi evaluator thành tùy chọn mặc định cũ.

## 4. Trạng thái sau P3–P4

P0–P2 đã có evaluator nhất quán và các sửa lỗi điều khiển. P3–P4 bổ sung charging repair với ba target CTest đạt 34 ca; 18 run đối chiếu trên r104/r101/c109 trả về 221 nghiệm, 2.792 tuyến đều qua validator. Mode on có distance đại diện tốt hơn ở 4/9 cặp, HV 3D tốt hơn ở 6/9 cặp; số xe cuối không đổi. Chưa có lợi ích nhất quán để bật mặc định. Code ở commit 28a9057, API và cờ thử nghiệm được giữ để nghiên cứu tiếp.

Cache helper P5 đã được đo và giữ lại vì giảm thời gian trong sáu cặp kiểm tra, đồng thời giữ nguyên kết quả. Nếu tiếp tục tối ưu, đo riêng copy/snapshot/evaluator trong ejection trước khi đổi cách tính; không giảm độ sâu chỉ để tạo lợi thế thời gian. Ngân sách wall time vẫn mềm; các phép đo đã dùng đồng hồ monotonic. Không mở rộng mô hình ra khỏi EVRPTW-PR Euclidean đối xứng đã chốt.

Kế hoạch này không cam kết trước tỷ lệ tăng tốc hoặc mức giảm xe. Các quyết định bật tính năng dựa trên tính đúng và thí nghiệm ghép cặp đã mô tả.
