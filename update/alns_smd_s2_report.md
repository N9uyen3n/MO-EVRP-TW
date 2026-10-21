# SMD S2: bỏ copy thừa

Ngày 2026-10-21. Baseline hành vi là B1 `fc531b7` sau sửa SearchContext, không phải B0 trước sửa.

## Thay đổi

KNN nhận vector output do caller sở hữu. Relocate, Or-opt và Or-opt đảo đoạn giữ buffer ngoài vòng duyệt candidate, tái sử dụng capacity giữa các lời gọi helper. TimeAware có overload ghi vào output, nên nhánh fallback không tạo vector trả về mới. Wrapper trả giá trị vẫn phục vụ các caller khác. Giữ score, partial_sort và top-K hiện tại.

Trong evaluateMove, `std::optional<Route>` chỉ tạo trial route thứ hai khi hai tuyến khác nhau. Các nhánh intra chỉ copy/đánh giá tuyến đầu. Không tái sử dụng Route global/static, không thay evaluator, acceptance, thứ tự duyệt, ngân sách hay mặc định ALNS.

## Kiểm thử đã hoàn tất

- **77 ca / 8 target Release đạt**:69ca sau S1, thêm7ca KNN và1ca differential evaluate/apply.
- KNN test sau warmup đo384lời gọi qua ba đường: KNN thường, route không có KNN customer nên fallback, node trạm không có KNN entry. **0 cấp phát** trong phạm vi đo; checksum đảm bảo kết quả được tiêu thụ. Đây không phải tuyên bố toàn solver không cấp phát.
- Bao phủ output riêng khi gọi xen kẽ hai LocalSearch/buffer, top-K0/lớn, route0/1khách, vị trí hợp lệ và đường TimeAware fallback.
- Differential trial/commit trên32fixture Euclidean có trạm: thực sự exercise intra relocate/2-opt/swap, inter relocate/swap/Or-opt/cross-exchange và station removal. Kiểm tra evaluation giữ nguyên nghiệm nguồn; mọi move báo khả thi áp dụng thực, aggregate rồi qua validator độc lập và distance delta khớp.
- Probe review đã cập nhật để kiểm chứng bản sửa thay vì mong lỗi cũ. Phiên bản historical tái hiện lỗi vẫn ở commit0581344; báo cáo review giữ rõ mốc đó.

Build `build-audit-smd-s2`, GCC11.4 Release với cùng flags và P6ON. Build/test log ở `build-audit-smd-review/`, CTest log của build chứa số allocation. Giữ nguyên TestALNS.cpp của người dùng.

## Cổng kiểm chứng còn chờ

Đóng băng B2 rồi chạy5case ×2seed ×2bản,200vòng: objective/schedule phải byte-identical với B1 trước khi chốt S2. Chưa tuyên bố tăng tốc toàn solver. A/B30cặp lặp lại thuộc S5; chi phí applyMove quá nhỏ trong profile hiện có nên chưa refactor commit trial route.
