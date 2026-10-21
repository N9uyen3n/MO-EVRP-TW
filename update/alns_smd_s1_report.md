# SMD S0–S1: sửa SearchContext

Ngày 2026-10-21. Bài toán EVRPTW-PR chỉ giao hàng, Euclidean đối xứng, sạc một phần. Người dùng yêu cầu tiếp tục `plan.md` sau giờ nghỉ.

## Thay đổi

Patch giờ cập nhật cả tuyến clean có một dirty route mới lọt vào top-8, dù dirty route đó không có trong danh sách cũ. Các centroid dirty được tính trước; với tuyến clean không chứa dirty neighbor cũ, so dirty centroid mới với cutoff top-8 rồi chỉ dựng lại danh sách bị ảnh hưởng. Full rebuild và patch dùng chung builder, sắp theo khoảng cách bình phương rồi index tuyến.

Phép tính khoảng cách bình phương dùng `std::fma(dx, dx, dy*dy)` một cách tường minh. Trong regression ngẫu nhiên, phép tính dot product ở hai vị trí inline từng làm tròn khác nhau một ULP (81.111111111111100 và 81.111111111111114), khiến oracle và implementation xếp hai ứng viên gần bằng nhau khác thứ tự. Chốt phép tính ở builder, screening và oracle giúp hợp đồng thứ tự có thể kiểm chứng; không dùng comparator epsilon có nguy cơ mất tính bắc cầu.

S1 có thể thay các move được xét hoặc thứ tự chúng được xét. Chưa tuyên bố nghiệm cuối giống B0 hoặc chất lượng tốt hơn. Không thay evaluator, điều kiện khả thi, objectives hay ngân sách.

## Bằng chứng kiểm thử

- Trước sửa: 60 ca / 6 target Release đạt lại; audit binary SHA256 vẫn `420016ebbd7dda5744da6b5ccee941fd3a770831a1484d1623f0c64709cddd25`.
- Phiên bản regression đầu chạy với thư viện cũ: 4 pass / 4 fail, bắt được newly-close omission và thứ tự ties; log `build-audit-smd-review/context_before.log`. Đây là test failure đúng kỳ vọng trước sửa.
- Sau sửa: **69 ca / 7 target Release đạt**, gồm 9 ca SearchContext. Có 1.280 bước swap/reverse ngẫu nhiên trên 32 fixture Euclidean, so với oracle sort độc lập và full rebuild ở từng bước, replay độc lập mọi nghiệm.
- Bao phủ newly-close fixture 11 tuyến có swap hợp lệ giảm distance1998; tie ở cutoff; centroid trùng; 0/1/ít tuyến, tuyến chỉ depot; thêm/xóa tuyến, explicit invalidate khi reorder cùng số tuyến; ranking dirty dù centroid không đổi; commit thực Inter-2-opt và Or-opt đảo đoạn.
- Giữ riêng test topology: thay/reorder cùng số tuyến phải invalidate; patch không tự phát hiện mọi thao tác trên vector mà caller không thông báo.
- `freeze_smd_stage.py` và `run_smd_audit.py` qua Python syntax check; hành vi audit còn được kiểm chứng bằng các run thực ở bước kế tiếp.

Release build: `build-audit-smd-s1`, GCC11.4, cùng flags tối ưu và P6 ON như baseline. Raw build/test/diagnostic logs trong `build-audit-smd-review/`. Source/binary baseline B0 và 5 input đã lưu tại `artifacts/alns_smd/20261021/B0`, metadata `stage.json`, đối chiếu 85 file production với Git `0581344`.

## Việc còn lại

Đóng băng B1 và audit 5 case × 2 seed × 200 vòng. S1 được phép thay kết quả; kiểm chứng mọi output. S2 so với B1 phải giữ objective/schedule byte-identical. Chất lượng B0/B1 ở 60s × 5 seed/case và tốc độ B1/final chạy tại S5 sau khi hoàn tất các bước có lợi. Không dùng kết quả S1 như phép đo lợi ích riêng của copy/cache.

Plan được lưu trước sửa trong commit `0c21596`. Giữ nguyên các chỉnh sửa của người dùng trong TestALNS.cpp; không đưa dữ liệu/log khác vào commit.
