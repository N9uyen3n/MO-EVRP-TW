# Bộ notebook phân tích ablation FULL vs No-EQ (EVRPTW-PR)

Triển khai đầy đủ kế hoạch phân tích Q1 (Section 2–13) dựa trên cấu trúc log
`logs_test` (config, progress, operators, front_objectives, front_details,
summary, evolution_log).

## Cài đặt

```bash
pip install pandas numpy scipy matplotlib
```

Đặt `evrp_analysis_utils.py` cùng thư mục với các notebook, và trỏ
`FULL_DIR` / `NOEQ_DIR` (đầu mỗi notebook) tới hai thư mục benchmark thật
của bạn (mặc định `benchmark/full`, `benchmark/no-EQ`).

## Thứ tự chạy (mỗi notebook ghi output vào `artifacts/` cho notebook sau)

| # | Notebook | Nội dung | Tương ứng |
|---|----------|----------|-----------|
| 01 | `01_audit_manifest.ipynb` | Audit cấu hình, `run_manifest.csv` | Section 2–3 |
| 02 | `02_pareto_clean_fleet_target.ipynb` | Clean Pareto, common fleet target, TD solutions | Section 4–6.1 |
| 03 | `03_budget_curve_hypervolume.ipynb` | Budgeted equity curve, HV2D/HV3D | Section 6.2–6.3 |
| 04 | `04_route_fairness_details.ipynb` | Parse `_front_details.txt`, Gini/ρmax/CV/Jain | Section 7 |
| 05 | `05_evolution_case_studies.ipynb` | Case study từ `_evolution_log.txt` | Section 8 |
| 06 | `06_convergence_runtime.ipynb` | Convergence theo thời gian thực, runtime overhead | Section 9 |
| 07 | `07_statistics_final_tables.ipynb` | Wilcoxon paired, Table 1–4, Figure 1–3, gate check Q1 | Section 10–13 |

## ⚠️ Việc bạn cần làm trước khi chạy trên dữ liệu thật

Hai file log có định dạng **không được đặc tả chính xác** trong tài liệu bạn
cung cấp (`_front_details.txt` dạng cây, `_evolution_log.txt`):

- Notebook **04**, cell "4.1. Kiểm thử parser trên MỘT file mẫu" in ra 25 dòng
  đầu của một file `_front_details.txt` thật.
- Notebook **05**, cell "5.1" làm tương tự cho `_evolution_log.txt`.

Đối chiếu output với regex trong `evrp_analysis_utils.py`
(`_RE_SOLUTION_HEADER`, `_RE_VEHICLE_HEADER`, `_RE_CUSTOMER`, `_RE_STATION`,
`_RE_TIME_FIELD`, `_RE_ARRIVAL_DEPARTURE`, `_RE_EVOLUTION_LINE`) và chỉnh lại
cho khớp, sau đó chạy lại toàn bộ pipeline.

Các file còn lại (`_config.txt`, `_summary.txt` dạng key-value;
`_progress.csv`, `_operators.csv`, `_front_objectives.csv` dạng CSV có
header) đã có parser tổng quát nên nhiều khả năng chạy đúng ngay, nhưng vẫn
nên kiểm tra nhanh output notebook 01.

## Ghi chú thiết kế

- Tất cả so sánh Z2/Z3/Z4 đều được khoá theo **common fleet target**
  (Section 5) trước khi so sánh, tránh việc FULL "thắng" chỉ vì dùng thêm xe.
- `FinalArchiveSize` không được dùng làm bằng chứng chất lượng trực tiếp vì
  FULL có 1 objective nhiều hơn No-EQ (Z3).
- Thống kê ở notebook 07 dùng **instance làm đơn vị** (paired Wilcoxon +
  Hodges–Lehmann + bootstrap CI + Holm correction), đúng protocol Section 11.
- Table 2 (so sánh literature) cần bạn cung cấp thêm `literature.csv`
  (cột `Instance, PublishedFleet, PublishedDistance`) vì đây là dữ liệu
  ngoài log của bạn.

## Quy trình Tự động (Script Pipeline) mới cập nhật

Bên cạnh các notebook, folder này cung cấp sẵn các script Python để tổng hợp số liệu và render thẳng ra file LaTeX / PDF báo cáo. Trình tự chạy để xuất báo cáo:

1. **`generate_latex_tables.py`**: Sinh toàn bộ các bảng so sánh tổng quát (Hypervolume, Route Fairness, Runtime, Ablation...) ra các file `.tex` trong `artifacts/latex_tables/`.
2. **`generate_bks_comparison.py`**: Dành riêng để so sánh với Best Known Solutions (sinh ra Bảng 2).
3. **`generate_instance_details.py`**: Xử lý bảng phụ lục chi tiết (longtable) cho tất cả các instances (ví dụ quy mô `_21`).
4. **`analyze_route_lengths.py`**: Sinh các biểu đồ so sánh độ phân tán thời gian chạy từng xe (makespan, min route, time gap) cho những instance điển hình (vd `c101_21`, `r101_21`, `rc101_21`). File ảnh được ném vào `artifacts/`.
5. **Compile LaTeX**: Dùng `pdflatex report.tex` (nằm trong thư mục `artifacts`) 2 lần để kết xuất báo cáo cuối cùng (`report.pdf`). Báo cáo tự động include toàn bộ bảng số liệu và ảnh biểu đồ.

### Cách đưa kết quả lên GitHub (Deploy)
Toàn bộ quy trình sao chép file, lưu trữ artifacts và commit lên Git được gói gọn trong script `push_update.ps1`. Cách dùng:

Mở PowerShell tại thư mục này và gõ:
```powershell
.\push_update.ps1
```
Script sẽ tự động copy những script phân tích mới nhất, các file LaTeX/PDF, và toàn bộ ảnh biểu đồ vừa sinh để đẩy (push) lên nhánh `result1`.
