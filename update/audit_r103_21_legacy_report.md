# Kiểm tra log cũ `logs_testLS5/r103_21`

Ngày kiểm tra: 2026-10-20. Phạm vi chỉ kiểm tra log được yêu cầu; P3–P4 vẫn tạm dừng. Không chạy lại ALNS, không đổi source thuật toán hoặc ghi đè log gốc.

## Kết luận

**49/49 nghiệm giữ nguyên thứ tự khách và trạm đều có lịch sạc khả thi. Nhưng 49/49 lịch ghi trong log không hợp lệ như đã xuất.** Số xe và tổng khoảng cách được xác nhận; các mục tiêu phụ thuộc lịch cần kiểm tra/tính lại với lịch khả thi cụ thể.

| Kiểm tra | Kết quả |
|---|---:|
| Số nghiệm / số tuyến | 49 / 686 |
| Mỗi khách được phục vụ đúng một lần | 49/49 nghiệm |
| Tổng demand giao hàng không vượt tải | 49/49 nghiệm |
| Số xe | Tất cả 14 xe |
| Distance tính lại khớp CSV đến độ chính xác bốn chữ số | 49/49 nghiệm |
| Lịch log nhất quán cả thời gian, sạc và pin, đã xét làm tròn | 0/49 nghiệm; 173/686 tuyến |
| Giữ giờ và lượng sạc log, chỉ cho phép sửa trạng thái pin | 0/49 nghiệm; 506/686 tuyến |
| Cho phép lập lại lịch sạc, giữ nguyên toàn bộ thứ tự node | 49/49 nghiệm; 686/686 tuyến |

Đếm trực tiếp có 161 lần đến khách trễ deadline và 662 vị trí không bảo toàn pin giữa các node liên tiếp. Các số này đếm cả tuyến xuất hiện lại ở nhiều nghiệm; không phải số lỗi độc lập hoặc số khách khác nhau.

## Ví dụ có thể đối chiếu

Nghiệm 1, tuyến đầu tiên (Route ID 35):

- C68: log đến 70.94, deadline 67.00 (dòng 17 của front_details).
- C77: log đến 168.53, deadline 160.00 (dòng 21).
- C79 ghi pin 25.92; đi tới C33 tốn khoảng 5.656854, không sạc tại khách. Pin tại C33 phải khoảng 20.263146, nhưng log ghi 7.81 (dòng 19). Chênh lệch vượt xa sai số làm tròn.

Giữ nguyên tuyến `D0 → C55 → S1 → C68 → C79 → C33 → S3 → C77 → C76 → D0`, một lịch sạc kiểm chứng được có lượng sạc tại S1 khoảng 14.603348 và S3 khoảng 30.776661. Xe đến C68 lúc 67.000000, C77 lúc 153.471448, về depot lúc 208.652476. Không đổi vị trí khách/trạm hoặc thêm trạm.

Nghiệm 47, một trong các nghiệm distance nhỏ nhất:

- 14 xe, Distance tính lại **1309.02198943433**, khớp **1309.0220** trong CSV.
- Lịch cũ đến C84 lúc 107.47 > deadline 106.00 (dòng 12345), đến C65 lúc 78.25 > deadline 78.00 (dòng 12482).
- Cả 14 tuyến đều đã lập lại được lịch khả thi. Một lịch tái lập cho Gini **0.1080254637**, MaxTime **223.029857262**; CSV cũ ghi Gini **0.1034**, MaxTime **223.0299**.

Gini tái lập trên là giá trị của **một lịch khả thi**, không phải Gini tối ưu cho bộ tuyến đó. Sự khác nhau không chứng minh Gini 0.1034 không thể đạt bằng một lịch khác. Không được coi Gini tái lập là bản sửa duy nhất hoặc kết luận Pareto mới mà chưa chọn chính sách lập lịch và lọc lại front.

## Phương pháp và giới hạn

- Đọc trực tiếp dữ liệu `data/solomon/r103_21.txt`; bản `data/test-100/r103_21.txt` có cùng SHA256 `9dd6134e2efee07fd0cb34ac38032ec9b31bef459fd8d2071dd01dde86328d3c`.
- Dùng khoảng cách Euclidean, thời gian distance/v theo Instance hiện có. Q=62.14, tải=200, tiêu thụ=1, thời gian sạc/đơn vị năng lượng=0.48, vận tốc=1. Chỉ giao hàng, sạc một phần.
- Kiểm tra coverage/tải/tổng distance độc lập. Với log hai chữ số, xét khoảng làm tròn ±0.00500001 cho giờ, lượng sạc và pin; không kết luận lỗi từ chênh lệch do làm tròn.
- Dùng LP tuyến tính SciPy 1.17/HiGHS cho từng thứ tự node cố định. Ràng buộc gồm pin trước và sau sạc, giới hạn pin, quan hệ di chuyển, cửa sổ bắt đầu phục vụ, thời gian phục vụ, giờ trạm mở và hoàn tất sạc trước giờ đóng. Cho phép chờ; không thêm/bỏ/đổi thứ tự node. Pin đầu tuyến đầy và khởi hành theo giờ depot.
- LP lập lại lịch không tối ưu Gini hoặc bộ mục tiêu của ALNS. Từ lượng sạc LP tìm được, mô phỏng lại lịch phục vụ sớm nhất; sau đó đọc CSV lịch tái lập và kiểm tra độc lập thêm một lượt. **49 nghiệm/686 tuyến đều đạt**, coverage/tải/pin/TW/sạc/chỉ số đều được kiểm tra.
- Bảy ca kiểm tra nhỏ cho LP đều đạt, bao gồm không được sạc trước khi tới trạm, pin để về depot, trạm lặp, deadline bắt đầu phục vụ, giờ hoàn tất sạc và phân biệt lỗi pin với lịch hành động.
- Đây là xác nhận đối với dữ liệu hiện có. File cấu hình cũ không ghi đủ seed/toàn bộ tham số để tái hiện chính xác quá trình tìm kiếm. Chưa kiểm tra toàn bộ evolution log hoặc các instance khác.

## Ý nghĩa cho báo cáo

Không cần bỏ kết quả **14 xe, distance 1309.0220** của trường hợp này: đã có lịch khả thi làm bằng chứng cho nguyên bộ tuyến. Tuy nhiên, không nên dùng nguyên lịch chi tiết cũ hoặc tự động giữ nguyên Gini/Pareto front. Cần lưu lịch khả thi đi kèm, thống nhất cách lập lịch cho mọi nghiệm, tính lại Gini/MaxTime và lọc lại front khi sử dụng các mục tiêu đó.

Việc các bộ tuyến có thể lập lại lịch không chứng minh lỗi chỉ nằm ở định dạng xuất file: absorption cũ có thể sửa trạng thái và ảnh hưởng chỉ số trong quá trình chạy. Kiểm tra này xác nhận khả thi của bộ tuyến và distance, chưa chứng nhận mọi quyết định đánh giá/chọn nghiệm trong lần chạy cũ.

## Bằng chứng và chạy lại

- Kết quả cuối: [validation.json](audit_r103_21_legacy/verified/validation.json), [summary.csv](audit_r103_21_legacy/verified/summary.csv).
- Lịch khả thi đầy đủ độ chính xác: [reconstructed_schedules.csv](audit_r103_21_legacy/verified/reconstructed_schedules.csv).
- Kiểm tra lại lịch tái lập: [witness_recheck.json](audit_r103_21_legacy/verified/witness_recheck.json).
- Script: `src/test/validate_legacy_front.py`; kiểm tra LP: `src/test/test_validate_legacy_front.py`.
- Các file trực tiếp trong `audit_r103_21_legacy/` là lượt chạy đầu; thư mục `verified/` là lượt xác nhận sau kiểm tra script. Kết luận hai lượt giống nhau. SHA256 của dữ liệu/log gốc được lưu trong validation.json và đã kiểm tra không thay đổi.

```powershell
python -m unittest discover -s src/test -p test_validate_legacy_front.py -v
python src/test/validate_legacy_front.py data/solomon/r103_21.txt logs_testLS5/r103_21 update/audit_r103_21_new_run
```

Script cần NumPy/SciPy và từ chối ghi đè thư mục kết quả đã tồn tại.
