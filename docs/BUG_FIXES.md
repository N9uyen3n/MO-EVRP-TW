# Sửa lỗi ngày 28/10/2026

## ECR: ánh xạ đúng trạng thái pin

`getCustomers()` chỉ chứa khách, còn `getStates()` có một phần tử cho mỗi vị
trí trong `getNodes()`, kể cả depot và trạm. `calculateEnergyRiskScore()` nay
tách `customerPos` để chọn khách kế tiếp và `nodePos` để đọc pin của khách
đang xét. Trước đó khách đầu tiên đọc pin depot; các khách sau có thể đọc pin
của node khác.

Giữ nguyên công thức risk còn lại, ngưỡng 0.3 và chính sách tối đa một khách
bị xóa mỗi lần gọi. Đây là bản sửa mapping, chưa phải thiết kế lại ECR theo
`nodesToRemove` hay đánh giá đóng góp của ECR bằng ablation mới.

## RouteMergingDestroy: bộ lọc time window có tác dụng

Điều kiện cũ `(dueA < readyB) && (dueB < readyA)` không thể đúng khi các cửa
sổ hợp lệ. Nay xét hai thứ tự trực tiếp A→B và B→A, tính thời gian phục vụ,
di chuyển và chờ; cặp bị xem là xung đột nếu cả hai thứ tự đều không kịp cửa
sổ node thứ hai.

Giữ cách lấy tối đa 5×5 cặp và ngưỡng xung đột 60%. Đây là heuristic chọn
tuyến, không phải chứng nhận khả thi cho tuyến gộp. Repair và evaluator vẫn
phải kiểm tra toàn bộ pin, sạc, tải, lịch trình và depot. Việc tính thời gian
đi trực tiếp giữa hai khách cũng không thay thế việc đánh giá detour qua trạm.

## Tuyến không có khách

`Solution::evaluateRoutes()` gọi `removeEmptyRoutes()` trước khi tổng hợp số
xe, distance, Gini và MaxTime. Tuyến chỉ ghé trạm cũng bị loại. Kiểm tra khả
thi cấp lời giải và checker C++/Python từ chối tuyến sử dụng không có khách.

## Kiểm tra

`DestroyOperatorRegressionTests` có năm ca: pin khách đầu, mapping khi có trạm
xen giữa, cửa sổ rời nhau nhưng phục vụ tuần tự được, xung đột do travel time,
và service time/đúng biên deadline. Với hai implementation cũ, bốn ca thất
bại; với implementation đã sửa, cả năm ca đạt.

Các test tuyến, local search, chính sách mục tiêu và giao diện single/benchmark
cũng được giữ. Các bản sửa không thay công thức thời gian sạc, chính sách
waiting absorption, số chiều HV hoặc tập mục tiêu Full/NoEQ.

Kết quả của source/binary cũ vẫn là dữ liệu lịch sử riêng. Một lượt chạy trên
bản sửa không chứng minh ảnh hưởng trung bình của lỗi; cần nhiều seed và cùng
điều kiện thực nghiệm để đánh giá điều đó.
