# Hướng dẫn Đẩy dữ liệu (Push) lên GitHub

Để lưu trữ lại các kết quả phân tích và báo cáo LaTeX vừa tạo, dưới đây là danh sách các file quan trọng bạn cần commit và push lên repository [MO-EVRP-TW](https://github.com/N9uyen3n/MO-EVRP-TW) tại nhánh `test01`.

## 1. Danh sách các file cần thiết

### 📄 Báo cáo LaTeX & Bảng biểu (PDF & TEX)
Những file này chứa nội dung báo cáo và mã nguồn LaTeX để dịch ra báo cáo:
- `artifacts/report.tex`: File báo cáo chính.
- `artifacts/report.pdf`: Bản PDF của báo cáo chính.
- `artifacts/detailed_report.tex`: File báo cáo phụ lục chi tiết từng instance.
- `artifacts/detailed_report.pdf`: Bản PDF của phụ lục chi tiết.
- `artifacts/instance_details.tex`: Bảng số liệu dài (longtable) nhúng vào phụ lục.
- `artifacts/latex_tables/*.tex`: Toàn bộ các bảng nhỏ được script tự động sinh ra.
- `artifacts/*.png`: Toàn bộ các biểu đồ hội tụ và budget curve (cần thiết để dịch file `report.tex`).

### 📊 Dữ liệu Thống kê (CSV)
Chỉ nên đẩy những file `.csv` kết quả tổng hợp thay vì đẩy toàn bộ raw log (để tránh repo bị quá nặng):
- `artifacts/solutions_clean.csv`: Dữ liệu tập Pareto đã làm sạch.
- `artifacts/td_solutions.csv`: Dữ liệu các nghiệm Distance-Focus.
- `artifacts/route_fairness_metrics.csv`: Chi tiết các chỉ số fairness tuyến xe.
- `artifacts/runtime_overhead.csv`: Dữ liệu thời gian chạy.
- `artifacts/hv2d_results.csv` & `artifacts/hv3d_results.csv`: Dữ liệu Hypervolume.
- `artifacts/fleet_attainment.csv` & `artifacts/fleet_targets.csv`: Kết quả fleet size.

### 🐍 Mã nguồn Python (Scripts)
Các script dùng để sinh bảng và xử lý:
- `generate_latex_tables.py`
- `generate_instance_details.py`
- `evrp_analysis_utils.py` (Do file này đã được vá lỗi regex xử lý chuỗi)

---

## 2. Các lệnh Git thực thi (Git Commands)

Bạn mở Terminal/Command Prompt tại thư mục gốc của project (nơi chứa thư mục `.git`) và chạy lần lượt các lệnh sau:

```bash
# 1. Đảm bảo bạn đang ở nhánh test01 và cập nhật code mới nhất
git checkout test01
git pull origin test01

# 2. Thêm các file Script Python
git add generate_latex_tables.py generate_instance_details.py evrp_analysis_utils.py

# 3. Thêm các file Báo cáo và Bảng biểu LaTeX
git add artifacts/report.tex artifacts/report.pdf artifacts/detailed_report.tex artifacts/detailed_report.pdf
git add artifacts/instance_details.tex artifacts/latex_tables/*.tex
git add artifacts/*.png

# 4. Thêm các file Dữ liệu CSV
git add artifacts/solutions_clean.csv artifacts/td_solutions.csv artifacts/route_fairness_metrics.csv
git add artifacts/runtime_overhead.csv artifacts/hv2d_results.csv artifacts/hv3d_results.csv
git add artifacts/fleet_attainment.csv artifacts/fleet_targets.csv

# 5. Commit dữ liệu với thông điệp rõ ràng
git commit -m "docs: Thêm báo cáo LaTeX chi tiết và kết quả ablation study trên quy mô _21"

# 6. Push lên nhánh test01
git push origin test01
```

> **Lưu ý nhỏ:** Một số file raw siêu lớn (ví dụ `solutions_raw.csv` hoặc các log text ban đầu) không nên push lên GitHub vì nó làm phình dung lượng kho chứa rất nhanh. Các file `.csv` gộp như trên là đủ để tái lập lại toàn bộ báo cáo rồi!
