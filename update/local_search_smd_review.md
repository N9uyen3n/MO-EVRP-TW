# Review MoveDescriptor và SMD trong LocalSearch

Ngày 2026-10-21, Git nền `407a32b`, thuật toán/binary tương ứng P6 `edd709b`. Yêu cầu: kiểm tra còn nâng cấp được phần static move descriptor không. Đây là review và ví dụ kiểm chứng, **chưa sửa thuật toán hoặc thay mặc định sau P7**. Giữ chỉnh sửa người dùng trong TestALNS.cpp riêng.

## Hiện trạng thực tế

`MoveDescriptor` ở `include/alns/LocalSearch.h:43` gồm enum, chỉ số, độ dài đoạn, savings và kết quả evaluation. Các toán tử đang dùng biến cục bộ, không dùng chung một static descriptor hoặc activeMove. Đo bằng GCC11.4/WSL Release: **88 byte, trivially copyable**, không có vector/heap trong descriptor. Chuyển riêng nó sang static không giải quyết chi phí copy Route, và sẽ bổ sung trạng thái dùng chung không cần thiết.

Phần có nhãn `[SMD]` đang triển khai là `SearchContext`: centroid, top-8 tuyến lân cận, ranking điểm tháo và cờ dirty. Chưa có bảng nhớ kết quả đánh giá từng move/cặp tuyến có route version. Một số chú thích cũ còn nói activeMove hoặc KNN trả tham chiếu, không khớp implementation hiện tại.

## Phát hiện đã xác nhận

### 1. Patch lân cận bỏ sót một tuyến vừa tiến lại gần

`src/alns/LocalSearch.cpp:922`: patch cập nhật tuyến dirty, cùng các tuyến có dirty route trong danh sách hàng xóm cũ. Nếu một dirty route trước đó nằm ngoài top-8 nhưng nay lọt vào top-8 của tuyến clean, tuyến clean đó có thể không được rebuild.

Đã dựng ví dụ Euclidean, delivery-only, 12 khách/11 tuyến, không cần sạc vì pin đủ lớn. Centroid ban đầu các tuyến: `0,10,20,...,80,1000,1001`. Tuyến 10 có hai khách ở x=1 và x=2001. Swap khách x=1000 của tuyến 9 với khách x=1 của tuyến 10 làm tuyến 9 có centroid1, giảm distance **1998**, mọi tuyến và customer coverage vẫn qua validator độc lập.

Probe gọi trực tiếp evaluate/apply một descriptor hợp lệ rồi kiểm tra hợp đồng patch so với full rebuild. Nó không đo tần suất ALNS thực tế gặp trạng thái này hoặc khẳng định chính chuỗi candidate của searchSwap sẽ chọn swap đó trước các granular filters.

```text
neighbors_before       0 1 2 3 4 5 6 7 8
neighbors_after_patch  0 1 2 3 4 5 6 7 8
neighbors_full_rebuild 0 9 1 2 3 4 5 6 7
```

Đây là thiếu cập nhật phạm vi tìm kiếm, có thể bỏ lỡ move. Nó **không làm các lịch P7 đã kiểm chứng trở thành bất khả thi**. Sửa điểm này sẽ thay đổi các move được xét và có thể đổi kết quả tìm kiếm, nên phải đánh giá chất lượng riêng, không yêu cầu byte-identical với heuristic cũ.

Hướng sửa: sau khi tính centroid mới, kiểm tra mọi tuyến clean với các centroid dirty, kể cả dirty route chưa từng nằm trong old neighbors; rebuild khi một dirty route có thể lọt top-8 hoặc thay vị trí trong danh sách. Với chỉ vài tuyến dirty, có thể sàng O(R×D) rồi rebuild những danh sách bị ảnh hưởng. Tie handling và thứ tự phải nhất quán với full rebuild; không chỉ xét danh sách cũ.

### 2. KNN vẫn copy vector dù chú thích nói không cấp phát

`include/alns/LocalSearch.h:268` và `src/alns/LocalSearch.cpp:4273` trả **`std::vector<size_t>` bằng giá trị**, dù bên trong dùng thread_local buffer. Trả một static/thread_local lvalue không phải NRVO cho biến tự động cục bộ. Caller `const auto&` chỉ kéo dài lifetime của vector trả về, không tự loại bản copy đó.

Probe đã warmup tám lần rồi đo 128 lời gọi: **128 cấp phát**, return type không phải reference. Đo riêng phần gọi KNN trên fixture cố định, không phải số cấp phát toàn solver.

Hướng sửa ưu tiên API output buffer hoặc scratch thuộc LocalSearch, với hợp đồng ownership rõ ràng. Đổi sang const reference cũng có thể bỏ copy nhưng phải kiểm tra mọi call-site: reference bị ghi đè khi gọi tiếp cùng helper, thread_local vẫn có thể dùng chung giữa nhiều LocalSearch trong cùng thread. Hiện ba call-site chính tiêu thụ kết quả trước lần gọi KNN tiếp theo; vẫn cần regression khi đổi API, không chỉ đổi chú thích.

### 3. Copy tuyến thứ hai thừa trong move nội tuyến

`src/alns/LocalSearch.cpp:565–568` dựng `r1_copy`, rồi luôn dựng `r2_copy`. Khi `twoRoutes=false`, r2_copy lại copy r1_copy, trong khi nhánh nội tuyến chỉ sửa/đánh giá r1_copy. Có thể chỉ dựng tuyến thứ hai khi cần, ví dụ optional/scratch hai tuyến có ownership cục bộ.

Ngoài ra, với các move đi qua nhánh copy/evaluate chung, evaluateMove đã có tuyến thử hợp lệ nhưng applyMove lại dựng thay đổi và đánh giá lại tuyến gốc. Có thể tái sử dụng tuyến đã đánh giá khi commit move được chọn. Không nên giữ toàn bộ Route trong mọi descriptor bị loại hoặc đưa chúng vào static dùng chung; inter-relocate hiện dùng virtual insertion nên cũng không nên buộc mọi ứng viên phải dựng tuyến đích chỉ để thống nhất API.

### 4. Muốn có SMD cache thật thì cần quản lý phiên bản và phạm vi

Descriptor hiện chứa vector index, không có identity/generation cho tuyến hoặc dấu vết phạm vi đã tìm. Hiện toán tử đánh giá rồi áp dụng ngay nên đây chưa phải bằng chứng có stale descriptor đang được dùng. Nếu giữ move để dùng lại, phải bảo vệ trước việc thêm/xóa/thay tuyến, đổi nghiệm ALNS và đổi phạm vi thích ứng.

Khóa tối thiểu tùy loại cache: operator, identity/generation của hai tuyến, các chỉ số/đoạn, weights, KNN/topK, maxNodesToCheck/maxSwapAttempts và phase/repair settings liên quan. Route::getId không đủ làm identity duy nhất. Width thích ứng tăng thì một kết quả “chưa thấy cải thiện” ở width nhỏ không được dùng để bỏ qua width lớn. Hết budget/repair trial không phải chứng minh bất khả thi.

Nên bắt đầu cache âm có giới hạn trong một phiên local search cho phần đánh giá xác định; xóa khi đổi nghiệm hoặc topology/identity thay đổi. Theo dõi hit/miss và so mode observe/off/on. Không cache ejection nhiều tuyến chỉ bằng một cặp tuyến: kết quả còn phụ thuộc các tuyến khác, depth, khách bảo vệ và ngân sách thử.

## Profiling lại bản hiện tại

Hai run Release seed1, 200 vòng cấu hình, safety3600s, P6/cache/undoON, charging repair/ScatterOFF, profilingON. Cùng binary SHA `420016ebbd7dda5744da6b5ccee941fd3a770831a1484d1623f0c64709cddd25`. Cả hai đủ200 vòng; 12 nghiệm/83 tuyến hợp lệ C++ + Python, objective/schedule byte-identical với control profilingOFF đã đóng băng từ P6/P7.

Mẫu số là tổng inclusive của hai entry point không lồng nhau `run` và `intensified_reduction`; các hàng con có thể lồng nhau, không cộng chúng. Đây là monotonic wall time có instrumentation, một mẫu/case, không phải A/B tăng tốc.

| Mục | r104_21 | rc205_21 |
|---|---:|---:|
| Tổng LS | 13,647s | 23,345s |
| VND distance | 19,44% | 6,61% |
| evaluateMove | **0,894%** (252.799 calls) | **0,638%** (166.150 calls) |
| applyMove | 0,021% | 0,024% |
| context rebuild + patch + removal ranking | 0,163% | 0,035% |
| Reconstruction | 52,28% | 11,56% |
| Ejection | 23,59% | 77,51% |

Do vậy không có cơ sở hứa cải thiện lớn bằng cách chỉ đổi descriptor hoặc gỡ copy trong evaluateMove. Ngay cả loại toàn bộ thời gian scope evaluateMove cũng chỉ tương ứng dưới1% LS trong hai mẫu này; đây là nhận xét cục bộ, không phải upper bound cho mọi cấu hình. Cache bỏ qua việc duyệt lân cận có thể tiết kiệm phần ngoài evaluateMove, nhưng phải đo tỷ lệ lặp và tác động tới search trước. Nút thắt giảm xe vẫn cần xem riêng.

TODO_EvaluateMove.md là ghi chú lịch sử: nhận định evaluateMove là bottleneck chính và kỳ vọng >50% chưa được dữ liệu hiện tại hỗ trợ. Cũng chưa tái hiện crash cũ để xác nhận duy nhất một nguyên nhân. Giữ các nhận định đó là lịch sử, không lấy làm mục tiêu cam kết.

## Thứ tự nâng cấp đề xuất

1. Sửa và kiểm chứng patch top-8 đúng so với full rebuild trên centroid gần/xa/tie, có thêm/xóa tuyến; đây là thay đổi phạm vi tìm kiếm. Giữ validation lịch và benchmark chất lượng riêng.
2. Tách bản tối ưu thuần tốc độ: bỏ KNN return copy, tránh r2_copy nội tuyến; thử commit tuyến đã đánh giá ở nhánh phù hợp. Cùng seed/số vòng phải giữ objective/lịch byte-identical, rồi đo A/B toàn solver.
3. Chỉ sau đó thử negative cache cho cặp tuyến/operator có generation và đầy đủ search scope, có giới hạn bộ nhớ và mode đối chiếu. Giữ nếu hit rate và lợi ích thực tế vượt chi phí; chưa cần rewrite mọi operator hoặc pooling static Route.

## Tái lập và hồ sơ

60 ca / 6 target CTest hiện tại đạt trước review; không sửa production nên không có bản solver “sau sửa” để tuyên bố tăng tốc. Probe độc lập: `src/test/probes/SMDReviewProbe.cpp`, không thêm vào CTest vì nó xác nhận hạn chế hiện tại; sau sửa cần regression kỳ vọng patch khớp full rebuild.

```text
c++ -std=c++17 -O3 -DNDEBUG -march=native -flto=auto -DALNS_INSERTION_TIME_BOUNDS=1 -Iinclude -Isrc/test src/test/probes/SMDReviewProbe.cpp build-audit-wsl-p6-bounds/libalns_lib.a -o build-audit-smd-review/review
./build-audit-smd-review/review
```

Raw: `build-audit-smd-review/` (Git ignore), có source probe ban đầu, executable/log, script profiling, CSV/validation và CTest log. Kết quả profiling máy đọc: `update/local_search_smd_review_profile.json`. Giữ nguyên thuật toán, cờ mặc định và kết quả P7; sửa SMD là bước mới sau khi chốt hướng triển khai.
