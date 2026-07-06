import pandas as pd
import numpy as np
import os

ARTIFACT_DIR = "artifacts"

# Read solutions and runtime
df_sol = pd.read_csv(f"{ARTIFACT_DIR}/solutions_clean.csv")
df_run = pd.read_csv(f"{ARTIFACT_DIR}/runtime_overhead.csv")

# Filter for _21 only
df_sol = df_sol[df_sol['Instance'].astype(str).str.endswith('_21')]
df_run = df_run[df_run['Instance'].astype(str).str.endswith('_21')]

# We want best and mean for each objective. 
# First, find the best (min) for each objective PER SEED
seed_best = df_sol.groupby(['Instance', 'Variant', 'Seed'])[['Z1', 'Z2', 'Z3', 'Z4']].min().reset_index()

# Now compute min (Best) and mean (Mean) across all seeds for each instance+variant
final_stats = seed_best.groupby(['Instance', 'Variant']).agg(
    Best_Z1=('Z1', 'min'),
    Mean_Z1=('Z1', 'mean'),
    Best_Z2=('Z2', 'min'),
    Mean_Z2=('Z2', 'mean'),
    Best_Z3=('Z3', 'min'),
    Mean_Z3=('Z3', 'mean'),
    Best_Z4=('Z4', 'min'),
    Mean_Z4=('Z4', 'mean')
).reset_index()

# Merge runtime
# runtime_overhead.csv has columns: Instance, FULL, No-EQ, Overhead_pct
# We need to melt it so we have Instance, Variant, Runtime
run_melt = pd.melt(df_run, id_vars=['Instance'], value_vars=['FULL', 'No-EQ'], var_name='Variant', value_name='Runtime')

final_df = pd.merge(final_stats, run_melt, on=['Instance', 'Variant'], how='left')

# Sort by Instance, then Variant
final_df = final_df.sort_values(by=['Instance', 'Variant'], ascending=[True, True])

# Generate LaTeX longtable
TEX_FILE = f"{ARTIFACT_DIR}/instance_details.tex"
with open(TEX_FILE, "w", encoding="utf-8") as f:
    f.write("\\setlength{\\tabcolsep}{3pt}\n") # Thu gọn khoảng cách giữa các cột
    f.write("\\setlength{\\LTleft}{0pt}\n")  # Căn sát lề trái
    f.write("\\setlength{\\LTright}{0pt}\n") # Căn sát lề phải
    f.write("\\begin{longtable}{@{\\extracolsep{\\fill}}llccccccccc@{}}\n")
    f.write("\\caption{Chi tiết Kết quả Từng Instance (Quy mô \\_21)} \\label{tab:instance_details} \\\\\n")
    f.write("\\toprule\n")
    f.write("\\textbf{Instance} & \\textbf{Variant} & \\textbf{Best $Z_1$} & \\textbf{Mean $Z_1$} & \\textbf{Best $Z_2$} & \\textbf{Mean $Z_2$} & \\textbf{Best $Z_3$} & \\textbf{Mean $Z_3$} & \\textbf{Best $Z_4$} & \\textbf{Mean $Z_4$} & \\textbf{Runtime (s)} \\\\\n")
    f.write("\\midrule\n")
    f.write("\\endfirsthead\n\n")
    
    f.write("\\multicolumn{11}{c}{\\tablename\\ \\thetable{} -- Tiếp tục từ trang trước} \\\\\n")
    f.write("\\toprule\n")
    f.write("\\textbf{Instance} & \\textbf{Variant} & \\textbf{Best $Z_1$} & \\textbf{Mean $Z_1$} & \\textbf{Best $Z_2$} & \\textbf{Mean $Z_2$} & \\textbf{Best $Z_3$} & \\textbf{Mean $Z_3$} & \\textbf{Best $Z_4$} & \\textbf{Mean $Z_4$} & \\textbf{Runtime (s)} \\\\\n")
    f.write("\\midrule\n")
    f.write("\\endhead\n\n")
    
    f.write("\\midrule \\multicolumn{11}{r}{Tiếp tục ở trang sau...} \\\\\n")
    f.write("\\endfoot\n\n")
    
    f.write("\\bottomrule\n")
    f.write("\\endlastfoot\n\n")
    
    prev_inst = None
    for _, row in final_df.iterrows():
        inst = row['Instance'].replace('_', '\\_')
        # Print instance name only on the first row of the instance group to make it cleaner
        disp_inst = inst if inst != prev_inst else ""
        
        f.write(f"{disp_inst} & {row['Variant']} & {row['Best_Z1']:.0f} & {row['Mean_Z1']:.2f} & {row['Best_Z2']:.2f} & {row['Mean_Z2']:.2f} & {row['Best_Z3']:.4f} & {row['Mean_Z3']:.4f} & {row['Best_Z4']:.2f} & {row['Mean_Z4']:.2f} & {row['Runtime']:.2f} \\\\\n")
        
        if prev_inst == row['Instance']:
            f.write("\\midrule\n") # add separator between instances
        
        prev_inst = row['Instance']
        
    f.write("\\end{longtable}\n")

# Generate standalone wrapper
WRAPPER_FILE = f"{ARTIFACT_DIR}/detailed_report.tex"
with open(WRAPPER_FILE, "w", encoding="utf-8") as f:
    f.write("\\documentclass[11pt,a4paper]{article}\n")
    f.write("\\usepackage[T5]{fontenc}\n")
    f.write("\\usepackage[utf8]{inputenc}\n")
    f.write("\\usepackage[vietnamese]{babel}\n")
    f.write("\\usepackage{booktabs}\n")
    f.write("\\usepackage{longtable}\n")
    f.write("\\usepackage{geometry}\n")
    f.write("\\geometry{margin=0.5in}\n\n")
    f.write("\\begin{document}\n")
    f.write("\\section*{Phụ lục: Kết quả Chi tiết từng Instance (Quy mô \\_21)}\n")
    f.write("Bảng dưới đây trình bày giá trị Tốt nhất (Best) và Trung bình (Mean) của các hàm mục tiêu $Z_1$ (Số xe), $Z_2$ (Quãng đường), $Z_3$ (Workload Gini), $Z_4$ (Thời gian dài nhất) cùng thời gian chạy tính bằng giây cho từng thuật toán, thống kê trên 10 lần chạy (seeds).\n\n")
    f.write("\\small\n")
    f.write("\\input{instance_details.tex}\n")
    f.write("\\normalsize\n")
    f.write("\\end{document}\n")

print("Generated instance_details.tex and detailed_report.tex successfully.")
