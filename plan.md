# Kế hoạch nâng cấp SMD / MoveDescriptor trong LocalSearch

Ngày lập: 2026-10-21. Git nền: `0581344`, nhánh `feat/alns-charging-repair-p3-p4`.

**Trạng thái 2026-10-21: người dùng đã đổi yêu cầu nghỉ thành tiếp tục ngay. S0–S4 và S5 gốc đã đủ dữ liệu; SMD chưa chứng minh tăng tốc ổn định. Điều tra bổ sung đã tách S2/S3 và trace S1; tìm thấy lỗi xóa trạm chỉ xét pin cũ, đã sửa ở B4 `128f1cd`, 90 ca / 9 target Release và apps đạt. Cache mặc định Off. Đang audit B3/B4 phạm vi và chất lượng, xem mục8 và điểm bàn giao.**

## 1. Mục tiêu và phạm vi

Sửa tính nhất quán của SearchContext, giảm cấp phát/copy không cần thiết và thử tránh duyệt lại những khối tìm kiếm đã thất bại. Mỗi thay đổi phải có bằng chứng tính đúng và số liệu trước/sau.

- Bài toán **EVRPTW-PR chỉ giao hàng**, sạc một phần, time windows, khoảng cách Euclidean đối xứng.
- Giữ evaluator sạc hiện tại làm đường đánh giá chuẩn. Cache không thay thế kiểm tra pin, tải, thời gian và coverage.
- Giữ thứ tự mục tiêu: ưu tiên số xe; cùng số xe xét Pareto Distance, Gini của active time và MaxTime. VND distance tiếp tục dùng tiêu chí giảm distance hiện tại.
- Giữ thứ tự duyệt first-improvement, KNN/top-K, ngưỡng và ngân sách tìm kiếm khi làm tối ưu thuần tốc độ.
- P7 đã hoàn tất theo phạm vi trước đó. Đây là đợt nâng cấp mới; các lịch P7 đã kiểm chứng vẫn có giá trị.
- Giữ riêng các chỉnh sửa của người dùng trong `src/test/TestALNS.cpp`, dữ liệu và log khác. Tại lúc lập plan, file này chọn `c108_21`, `scoreNonDominated=20`, `localSearchIntensity=15`; phải đọc lại Git khi tiếp tục vì người dùng có thể sửa thêm.

Tài liệu căn cứ: [review](update/local_search_smd_review.md), [profiling](update/local_search_smd_review_profile.json), [probe](src/test/probes/SMDReviewProbe.cpp). Những đề xuất cũ trong `smd_improvement_feasibility.md` và `TODO_EvaluateMove.md` phải đối chiếu lại với plan này trước khi triển khai.

## 2. Kết quả kiểm tra đã xác nhận

Đã đọc lại descriptor, evaluate/apply, tám toán tử VND, các đường sửa tuyến trực tiếp, cập nhật context, KNN và fallback, Route mutators, Solution::evaluateRoutes và các công cụ audit.

| Phát hiện | Bằng chứng | Quyết định |
|---|---|---|
| Descriptor đang là biến cục bộ, không chứa heap | Probe: 88 byte, trivially copyable trên GCC/WSL hiện tại | Giữ descriptor nhỏ; không chuyển thành đối tượng static dùng chung |
| Patch láng giềng bỏ sót tuyến mới tiến lại gần | Sau swap hợp lệ giảm distance 1998, patch giữ danh sách cũ nhưng full rebuild đưa tuyến 9 vào top-8 | Sửa trước khi thêm cache |
| KNN trả vector bằng giá trị dù comment nói const reference | Sau warmup: 128 lời gọi, 128 cấp phát | Đổi ownership của output, xử lý cả fallback |
| Intra move vẫn tạo bản copy tuyến thứ hai | `evaluateMove`: `r2_copy = twoRoutes ? target : r1_copy` | Chỉ tạo tuyến thứ hai khi thực sự là inter move |
| Có toán tử commit trực tiếp | Inter-2-opt, Or-opt đảo đoạn, repair và energy boost không đều đi qua applyMove | Theo dõi thay đổi ở mọi đường commit trong phạm vi cache |
| `isFeasible=false` không chỉ có nghĩa bất khả thi | evaluateMove còn return sớm khi distance không cải thiện | Cache phải ghi “không tìm thấy cải thiện trong phạm vi này”, không coi mọi false là chứng minh bất khả thi |
| Chưa có move cache có phiên bản tuyến | Descriptor dùng vector index; Route ID không bảo đảm duy nhất | Cần session/topology epoch và generation trước khi tái sử dụng kết quả |

Probe kiểm tra trực tiếp một swap hợp lệ và hợp đồng patch; không chứng minh full searchSwap sẽ chọn chính swap đó, cũng chưa đo tần suất lỗi patch trong ALNS thực tế. Đây là thiếu cập nhật phạm vi tìm kiếm, không phải bằng chứng lịch xuất ra sai pin/TW.

Đã chạy lại **60 ca / 6 target CTest Release: tất cả đạt**, và probe tái hiện đúng các số liệu trên. Binary audit hiện tại vẫn có SHA256 `420016ebbd7dda5744da6b5ccee941fd3a770831a1484d1623f0c64709cddd25`.

Profiling có sẵn trên r104_21 và rc205_21, seed 1, 200 vòng cấu hình:

| Scope | r104_21 | rc205_21 |
|---|---:|---:|
| Tổng LS | 13,647 s | 23,345 s |
| VND distance / tổng LS | 19,44% | 6,61% |
| evaluateMove / tổng LS | 0,894% | 0,638% |
| applyMove / tổng LS | 0,021% | 0,024% |
| searchOrOpt | 0,654 s | 0,608 s |

Các scope con có thể lồng nhau, không cộng phần trăm. Đây là hai mẫu profiling, không phải phép đo tăng tốc A/B. Chưa có căn cứ hứa tăng tốc lớn bằng cách chỉ sửa descriptor. Or-opt đáng đo việc duyệt lặp vì chi phí cả toán tử lớn hơn phần evaluateMove; khả năng cache có lợi vẫn cần kiểm chứng.

## 3. Thứ tự triển khai

| Bước | Công việc | Điều kiện hoàn thành |
|---|---|---|
| S0 | Kiểm tra Git, đóng băng baseline và cấu hình | Truy được source, binary, input, flags và kết quả trước sửa |
| S1 | Sửa patch SearchContext và thống nhất thứ tự láng giềng | Patch bằng full rebuild sau mọi tình huống kiểm thử; nghiệm hợp lệ |
| S2 | Bỏ copy output KNN và copy tuyến thứ hai ở intra move | Giữ nguyên candidate order, lịch và objective ở fixed iterations |
| S3 | Đo số khối Or-opt được duyệt lại, chế độ observe | Không đổi kết quả; biết hit tiềm năng, chi phí quản lý và phạm vi phụ thuộc |
| S4 | Thử cache âm có giới hạn và generation nếu S3 có triển vọng | Off/observe/on tương đương về kết quả; lợi ích vượt overhead |
| S5 | A/B cuối, kiểm chứng độc lập, chọn default và bàn giao | Báo cáo đầy đủ cả thắng/thua, commit riêng, hướng dẫn tiếp tục |

S1 là thay đổi phạm vi tìm kiếm. S2–S4 là tối ưu cần giữ nguyên hành vi so với **bản đã sửa S1**. Không trộn hai loại thay đổi rồi yêu cầu kết quả giống bản lỗi cũ.

### S0 — Baseline và điểm khôi phục

1. Chạy `git status`, đọc diff hiện tại; không stage hoặc ghi đè phần người dùng sửa.
2. Ghi Git SHA, bản chụp source cần build, compiler/flags, SHA binary và SHA input. Giữ binary P6/P7 hiện có, dùng thư mục build/output mới cho SMD.
3. Build WSL Ubuntu-22.04 Release, chạy CTest và lưu log trước thay đổi. Lần lập plan đã đạt; khi tiếp tục chỉ chạy lại nếu source/build thay đổi hoặc cần xác minh baseline mới.
4. Dùng `ALNSAuditRunner` với cấu hình cố định; không phụ thuộc các chỉnh sửa đang diễn ra trong TestALNS.
5. Tạo regression cho phát hiện patch trước khi sửa: test phải thất bại trên baseline vì patch khác full rebuild. Probe hiện tại xác nhận lỗi bằng exit 0 nên không đưa nguyên trạng vào CTest.

Các mốc đối chiếu: **B0** = bản hiện tại; **B1** = sửa S1; **B2** = B1 + tối ưu S2; **B3** = thêm cache nếu đạt S4. Commit từng bước độc lập, rollback bằng commit riêng, không reset thay đổi của người dùng.

### S1 — Cập nhật láng giềng đúng sau move

File chính: `include/alns/LocalSearch.h`, `src/alns/LocalSearch.cpp`; regression riêng hoặc bổ sung `LocalSearchRegressionTests.cpp`.

Hợp đồng: sau mỗi move được chấp nhận và patch, centroid và **danh sách láng giềng có thứ tự** phải bằng full rebuild trên cùng nghiệm. Ranking của tuyến đã đổi phải được đánh dấu dirty.

Thiết kế:

1. Dùng chung hàm dựng top-8 cho full rebuild và patch. Sắp theo `(distanceSquared, routeIndex)` để xử lý khoảng cách bằng nhau nhất quán. Việc chốt tie-break thuộc S1 vì có thể đổi thứ tự tìm kiếm so với B0.
2. Thu thập các tuyến dirty rồi tính lại centroid của chúng trước khi xét ảnh hưởng tới tuyến khác.
3. Rebuild danh sách của tuyến dirty và tuyến clean đang chứa một dirty neighbor.
4. Với các tuyến clean còn lại, so từng dirty centroid mới với phần tử cuối top-8 cũ. Nếu dirty route có thể chen vào theo cùng comparator, rebuild danh sách clean đó.
5. Không có dirty route thì không làm thêm. Context invalid hoặc số tuyến đổi thì full rebuild. Thêm/xóa/sắp xếp lại/thay toàn bộ nghiệm phải invalidate; không chỉ dựa vào số tuyến vì topology có thể đổi mà số lượng giữ nguyên.
6. Chỉ xóa centroidDirty sau khi xử lý đủ các danh sách. Ranking vẫn lazy; không dựng lại ranking của tuyến không đổi.

Chi phí sàng ảnh hưởng khoảng O(R×D + R×K), cộng chi phí dựng danh sách bị ảnh hưởng; K=8. Không hứa toàn bộ patch luôn O(R×D). Full rebuild dùng làm oracle trong test, có thể dùng làm fallback đơn giản nếu patch khó kiểm chứng.

Test bắt buộc:

- Fixture 11 tuyến đã tái hiện: dirty route từ ngoài top-8 tiến vào gần.
- Dirty neighbor đi xa; nhiều tuyến dirty cùng lúc; khoảng cách bằng nhau; centroid trùng nhau.
- Ít tuyến hơn giới hạn K, một tuyến, không có tuyến, tuyến rỗng trong giai đoạn trung gian.
- Thêm/xóa tuyến; thay hoặc đảo thứ tự tuyến nhưng giữ nguyên số lượng qua đường invalidate thích hợp.
- Chuỗi thay đổi Euclidean có seed cố định: so patch với dựng mới ở mọi bước; kiểm tra ranking dirty và coverage/lịch cho các move được chấp nhận.
- Chạy qua các đường commit VND trực tiếp, không chỉ qua applyMove.

S1 không yêu cầu nghiệm cuối giống B0. Đánh giá chất lượng theo ngân sách thời gian bằng nhau và validator độc lập. Nếu chất lượng giảm trên một số seed, báo rõ và điều tra; không lấy patch cũ làm chuẩn tính đúng.

### S2 — Bỏ copy thừa, giữ thứ tự tìm kiếm

**S2a: KNN output.** Ưu tiên API nhận output buffer của caller, giữ buffer ngoài các vòng duyệt candidate để tái sử dụng capacity. Cập nhật ba caller: relocate, Or-opt và Or-opt đảo đoạn. TimeAware fallback cũng cần đường ghi vào buffer; có thể giữ wrapper trả giá trị cho caller khác để hạn chế phạm vi sửa.

- Giữ nguyên phép tính score, partial_sort, top-K và thứ tự trả vị trí. Không nhân tiện đổi tie-break của KNN trong bước này.
- Test nhánh có KNN, không có neighbor trong tuyến nên fallback, tuyến ngắn/rỗng, top-K=0 và lớn hơn số ứng viên.
- Test hai output buffer và hai LocalSearch dùng xen kẽ để bảo đảm kết quả trước không bị ghi đè.
- Đo allocation sau warmup khi kích thước/capacity ổn định: mục tiêu không cấp phát output mỗi lần gọi, kiểm tra cả fallback. Không gọi đó là “toàn solver không allocation”.

**S2b: trial route.** Chỉ tạo `r2_copy` khi hai tuyến khác nhau, ví dụ dùng `std::optional<Route>`. Giữ canonical evaluation, kiểm tra cả tuyến nguồn/đích, reset MoveEvaluation và cơ chế áp dụng như cũ. Test các move intra/inter đang hỗ trợ, dịch chuyển hai hướng, đoạn gần depot, có trạm và move thất bại.

Điều kiện qua S2: fixed seed + fixed iterations cho objective và schedule đầy đủ byte-identical so với B1. Nếu khác, tìm nguyên nhân trước khi đo tốc độ. Các bộ đếm đúng bản chất có thể giảm; không yêu cầu số allocation hoặc số phép tính bị loại vẫn bằng nhau.

### S3 — Đo khả năng cache trước khi bật bỏ qua tìm kiếm

Phạm vi đầu tiên: **khối xét vị trí chèn của searchOrOpt**, vì khối này xác định và không gọi charging repair/energy boost. Chưa mở cache cho mọi operator.

Thứ tự hiện tại là `(segmentLength, r1, sourcePosition, r2, insertionPosition)`. **Không đổi thành vòng lặp theo cặp tuyến**, vì sẽ đổi move first-improvement được chọn.

Đơn vị ghi nhớ dự kiến: với một bộ `(segmentLength, r1, sourcePosition, r2)`, đã duyệt hết danh sách vị trí chèn theo scope hiện tại mà không tìm thấy move được chấp nhận. Lookup đặt trước KNN để có thể tiết kiệm cả dựng candidate, các filter và đánh giá tuyến. Chỉ ghi kết quả sau khi khối hoàn tất; return sớm vì thành công hoặc bị ngắt không được ghi là thất bại.

Phân biệt rõ:

- Khối không có cải thiện trong KNN/top-K hiện tại không chứng minh không tồn tại move cải thiện ở mọi vị trí.
- Không ghi “cặp tuyến đã duyệt hết” khi mới thử một khách hoặc một độ dài đoạn.
- Không dùng trực tiếp `!move.eval.isFeasible` làm khóa kết luận bất khả thi.

Chế độ `Observe` vẫn thực hiện toàn bộ tính toán; đếm lookup, repeated block, số candidate, số evaluate thực tế, số lần thay phiên bản, dung lượng và thời gian quản lý. Nếu key lặp nhưng khối nay tìm thấy cải thiện, coi là lỗi phạm vi/invalidation và dừng thử cache. Đo trên r104/r101/c109/rc106/rc205 với fixed iterations; số liệu profiling bật được lưu riêng khỏi timing chính thức.

### S4 — Cache âm có phiên bản, chỉ triển khai tiếp nếu S3 đáng làm

1. Cache thuộc LocalSearch, giới hạn trong **một lần runDistanceOptimization/VND** ở bản đầu. RAII hoặc cơ chế tương đương xóa ở đầu/cuối, kể cả exception. Không chia sẻ với lần ALNS khác hoặc intensified vehicle reduction.
2. Khóa dùng session/topology epoch + route index trong epoch + generation tuyến; thêm loại khối/toán tử, vị trí nguồn, độ dài đoạn và các tham số scope ảnh hưởng kết quả. Route::getId và địa chỉ Route không được dùng làm identity duy nhất.
3. Scope bao gồm top-K, ngưỡng granularity/epsilon, weights thực sự dùng, chính sách lọc. Các giá trị cố định cả session có thể quản lý bằng config epoch. Khi mở sang toán tử khác phải thêm adaptive width, hướng đoạn và các phụ thuộc của toán tử đó.
4. Generation ở lớp LS tăng khi commit đổi tuyến, kể cả đảo thứ tự nhưng centroid không đổi. Tập trung thông báo thay đổi ở helper chung cho applyMove, Inter-2-opt, Or-opt đảo đoạn, tryRepairRoutes và energy boost. Tránh thông báo lặp; structural change xóa cache và dựng lại mapping.
5. Energy boost hiện có nhánh gọi `solution.evaluateRoutes()`, có thể xóa tuyến rỗng và dịch index: phải xử lý topology trước khi tái dùng key. Charging/vehicle reduction ở ngoài session cache sẽ được xóa tại ranh giới VND.
6. Khởi đầu tối đa 4096 entry, giới hạn bộ nhớ rõ ràng và ghi sizeof thực tế. Hash chỉ để tìm entry; phải so **toàn bộ key**. Collision/eviction chỉ được làm mất hit, không được tạo hit sai. Bloom filter đơn độc không được phép quyết định bỏ qua move.
7. Có ba mode Off / Observe / Enabled. Giữ Off làm reference trong thời gian thử. Hit chỉ bỏ qua đúng khối không cải thiện đã ghi; không lưu Route hay move thành công để áp dụng về sau.

Test bắt buộc: hit khi tuyến/scope không đổi; thay từng tuyến; đảo đoạn giữ centroid; đổi top-K/scope; route ID trùng; thay/xóa/chèn/sắp xếp tuyến; hai nghiệm khác nhau cùng số tuyến; exception/return sớm; đầy cache/collision; không bỏ qua fallback có thể thành công. So cả accepted-move trace trên fixture và schedule/objective cuối ở Off/Observe/Enabled.

Nếu hit ít hoặc lookup/invalidation tốn hơn phần tiết kiệm, kết thúc S4 với kết luận không bật cache. Chỉ mở thêm toán tử sau khi có bằng chứng có lợi, bằng commit và audit riêng.

## 4. Các hướng để sau và lý do

- **Commit lại trial route đã evaluate:** khả thi cho một số nhánh, nhưng applyMove chỉ khoảng 0,02% LS trong hai mẫu. Chưa ưu tiên refactor lớn. Inter-relocate đang dùng virtual insertion; không buộc dựng tuyến đích cho mọi candidate chỉ để thống nhất API.
- **Static/global Route pool:** chưa có bằng chứng lợi ích đủ lớn; làm ownership, instance, vehicle và lifetime phức tạp hơn. Lịch sử crash chưa được tái hiện để kết luận nguyên nhân duy nhất.
- **O(1) feasibility hoặc evaluateFrom chỉ chạy suffix:** thay đổi đoạn sau có thể đổi nhu cầu pin và sạc ở trạm trước đoạn, kéo theo trạng thái forward trước điểm sửa. Không được mặc định prefix schedule bất biến. Giữ evaluator đầy đủ; P6 đã có bound chèn bảo thủ.
- **Lọc bằng timeSlack/energySlack của lịch cũ:** chưa có chứng minh bảo thủ khi lịch sạc thay đổi. Không đưa đề xuất cũ này vào hot path để loại move.
- **Periodic rebuild mỗi 30–50 move hoặc reverse-neighbor list:** không giải quyết ngay việc một dirty route từ ngoài danh sách tiến lại gần. S1 phải đúng sau từng accepted move.
- **Multi-neighbor theo workload, incremental Gini và thay tiêu chí chấp nhận:** đây là thay đổi thuật toán/mục tiêu tìm kiếm, cần kế hoạch chất lượng riêng.
- **Cache ejection chỉ bằng cặp tuyến:** thiếu trạng thái các tuyến khác, khách được bảo vệ, độ sâu, thứ tự và budget; không nằm trong SMD đợt này.

## 5. S5 — Quy trình kiểm thử và benchmark

Các case dùng lại: **r104_21, r101_21, c109_21, rc106_21, rc205_21**. Mọi run phải lưu input/source/binary hashes, seed, cấu hình, stop reason, số vòng thực và thời gian. Không suy kết quả từ tên thư mục.

| Mục đích | So sánh và protocol | Tiêu chí |
|---|---|---|
| Regression | CTest trước/sau từng bước, thêm ca SMD có ý nghĩa | Tất cả đạt; test phát hiện patch phải fail trên B0 và pass sau sửa |
| Kiểm tra không đổi hành vi | 5 case × seed 1,2; 200 vòng cấu hình, safety 3600 s | S2 so B1; cache Off/Observe/On cùng source. Đủ 200 vòng, objective/schedule byte-identical |
| Tốc độ cuối | B1 so bản tối ưu được giữ; 5 case × 2 seed × 3 lần lặp × 2 bản = 60 run / 30 cặp | Warmup riêng, cân bằng AB/BA, tuần tự, profiling tắt; ghi cả cặp chậm hơn |
| Chất lượng do S1 | B0 so B1; 5 case × seed 1–5 × 2 bản; search budget 60 s, maxIterations 25000 | Fleet trước, rồi distance/Gini/MaxTime và HV có cùng scale/reference; báo mọi đánh đổi |
| Tính hợp lệ | Mọi nghiệm được xuất trong audit | C++ validator và Python replay trên CSV đủ precision; không sửa/reschedule để làm kết quả pass |

Cấu hình audit chính nối tiếp P6/P7: LS intensity 25, scoreNonDominated 25, reconstruction cache 2, selective undo 1, P6 bounds ON, charging repair OFF, Scatter OFF. Cấu hình này khác TestALNS người dùng; báo cáo phải ghi rõ. Test chức năng riêng vẫn bao phủ đường repair để bảo đảm hook invalidation đúng.

Chỉ chuyển sang benchmark nhiều lần khi regression và fixed-iteration gate đã qua. Nếu dừng vì time_limit/HV trước số vòng dự kiến, không dùng run đó làm cặp đo cùng công việc. Không xem 200 vòng cấu hình là prefix tương đương của cấu hình 25000 vòng vì ngưỡng thích ứng phụ thuộc tổng vòng.

Thước đo tốc độ: giảm thời gian mỗi cặp `100 × (T_before − T_after) / T_before`; báo trung vị theo case, seed, toàn bộ cặp, tổng wall time và độ phân tán. Với kết quả sát nhiễu hoặc trái chiều, chỉ tăng lặp cho case còn bất định; không chọn lọc bỏ run thua. Mức nhỏ chưa rõ thì ghi “chưa chứng minh tăng tốc”. Không cộng phần trăm mới vào mức cải thiện P5/P6 cũ.

Thay đổi S1 có thể đổi lượng công việc tìm kiếm, nên thời gian B0/B1 không được dùng để cô lập lợi ích bỏ copy/cache. Lợi ích thuần tốc độ phải đo từ B1. Quality run báo cả phần vượt time budget do deadline mềm; số vòng trong 60 s không đồng nghĩa speedup cùng công việc.

## 6. Điều kiện chốt và bàn giao

- S1: patch tương đương oracle full rebuild, ranking và dirty flags nhất quán; ghi nhận ảnh hưởng chất lượng.
- S2: giảm đúng cấp phát/copy mục tiêu, kết quả fixed iterations không đổi, không có regression hiệu năng đáng kể chưa giải thích.
- S3/S4: biết chi phí và hit rate; chỉ bật cache mặc định nếu audit tương đương và A/B chứng minh có lợi. Cache không có lợi được ghi thành kết luận hoàn tất thí nghiệm, không cố bật cho đủ tính năng.
- Mọi nghiệm audit đạt pin, tải, TW khách/trạm/depot, coverage và objective qua validator độc lập.
- Mỗi bước có commit riêng và log kiểm thử. Stage theo tên file; không `git add .` để tránh đưa phần người dùng vào commit.
- Bàn giao report Markdown + JSON/CSV, manifest completed, hashes, cấu hình default cuối, các nhánh đã hoãn và điểm tiếp tục. Cập nhật checkbox dưới đây theo kết quả thực tế.

## 7. Checklist khi quay lại

- [x] Kiểm tra Git và đọc kỹ các đường evaluate/apply/context.
- [x] Chạy lại 60 ca / 6 target Release và probe SMD.
- [x] Ghi plan và tạm dừng; chưa sửa production.
- [x] S0: kiểm tra thay đổi mới sau giờ nghỉ, đóng băng baseline cho triển khai.
- [x] S1: regression + sửa patch và tie-break; 69 ca / 7 target Release đạt. Quality audit B0/B1 đủ 25 cặp trong S5, kết quả có cả giảm.
- [x] S2a: KNN output/fallback;128lời gọi probe0allocation,384lời gọi qua3đường trong regression0allocation.
- [x] S2b: bỏ copy tuyến thứ hai của intra move; B1/B2 giữ nguyên lịch/objective10/10cặp.
- [x] S3: Observe20run,178nghiệm/2.004tuyến hợp lệ,0mismatch; tỷ lệ block lặp theo run11,09–22,78%.
- [x] S4: Enabled20run,10/10cặp giữ kết quả; so Observe xác nhận3.959.545block skip đúng. Mặc định Off, chưa có bằng chứng đủ mạnh để bật.
- [x] S5 tốc độ: 60 run / 30 cặp giữ lịch/objective; median giảm 1,479%, tổng thời gian tăng 1,18%. Chưa chứng minh tăng tốc ổn định.
- [x] S5 chất lượng: 50 run, 734 nghiệm / 8.255 tuyến hợp lệ; cùng fleet mọi cặp; distance 7 thắng / 6 hòa / 12 thua, HV 11 thắng / 5 hòa / 9 thua.
- [x] Lặp riêng rc205: 12 run / 6 cặp giữ lịch/objective; 3/6 nhanh hơn, median giảm 1,126%, khoảng −4,732% đến +20,006%. Trái chiều đợt đầu, không gộp để đổi trọng số case.
- [x] Kiểm tra toàn vẹn cuối đạt: 202 run + 4 warmup, 2.091 nghiệm / 22.665 tuyến hợp lệ; source/binary/flags/raw/report/user-file khớp. Đã lưu checkpoint audit; các benchmark trên đã kết thúc.
- [ ] S5 kết luận toàn plan: đã điều tra bổ sung nhưng chưa chứng minh ưu thế chung; chờ audit guard B4 ở mục8 trước khi chốt.

**Điểm tiếp tục hiện tại: đọc `update/ALNS_SMD_RESUME.md`, kiểm tra Git và hồ sơ toàn vẹn. Không chạy lại các batch đã completed. Ưu tiên phân biệt B1/B2/B3Off trên rc205, rồi điều tra quỹ đạo S1 ở các seed r101/rc106 giảm chất lượng. Giữ cache Off; chưa mở thêm toán tử/cache.**

## 8. Bổ sung sau điều tra S5 — guard xóa trạm

Phép tách rc205 đã completed, 4/4 cặp mỗi bước giữ kết quả: S2 median −1,205% (2/4 nhanh hơn), S3Off median +0,835% (3/4 nhanh hơn, nhưng một cặp −14,698%). Chưa chứng minh tăng tốc ổn định. Không quy nguyên nhân cho một bước chỉ từ các mẫu trái chiều.

Trace B0/B1 độc lập trên r101/rc106 seed1, giữ maxIterations25000 và dừng chẩn đoán sau 8 vòng, đã xác định khác biệt đầu tiên là tie-order ngay đầu VND vòng0, sau đó Relocate chọn move khác. B1 có 1.037 snapshot context khớp full-sort oracle; B0 còn bỏ sót láng giềng gần hơn ở các patch sau. Sáu nghiệm / 112 tuyến cuối của trace hợp lệ; không coi các run có instrumentation này là benchmark.

Phát hiện thêm một lỗi cần sửa trong phạm vi tính đúng của LS: `removeRedundantStations(Route&)` commit chỉ bằng pin/minBatteryReq của lịch cũ. Tuyến r101 D0–S7–C19–S8–C36–C49–D0 hợp lệ, nhưng xóa S7 làm phải sạc lâu hơn tại S8 và trễ C36. ALNS đã chặn ứng viên ở bước kiểm tra sau LS; chưa có bằng chứng lỗi này lọt vào front audit đã kiểm chứng. Không suy từ phát hiện này rằng mọi lịch đã xuất trước đó sai.

- [x] Tái hiện trước sửa: thêm 3 regression; bản chưa sửa có 12 pass / 2 fail, gồm ca xóa trạm an toàn vẫn pass.
- [x] Sửa nhỏ: giữ energy screen làm bộ lọc; dựng candidate, đánh giá canonical toàn tuyến và chỉ commit khi feasible. Không đổi evaluator hoặc mục tiêu.
- [x] Build Release riêng `build-audit-smd-s4`, 90 ca / 9 target và apps đạt; giữ source người dùng (hiện đã đổi sang rc205_21, score20/LS15).
- [x] Commit riêng guard + regression `128f1cd`, đóng băng B4, audit binary SHA `e8d09486bbde49bc42747750671955189e271d60d83613029125b40404fe7029`.
- [ ] Audit B3/B4 cacheOff trên 5 case × seed1,2 × 200 vòng. Cho phép đổi kết quả vì đây là sửa tính đúng, không dùng wall time để gọi speedup thuần.
- [ ] Đánh giá chất lượng B3/B4 theo cùng protocol 60s / maxIter25000, 5 case × seed1–5; báo đầy đủ mọi thắng/thua. Đây là experiment riêng, không thay kết quả B0/B1 của S1.
- [ ] Lưu hồ sơ/hash/source/default cuối và kết luận theo dữ liệu; cache vẫn Off, không mở thêm tối ưu khác để làm đẹp số.
