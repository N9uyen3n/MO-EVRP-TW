import os
import re
import numpy as np
import pandas as pd
import scipy.stats as stats
import matplotlib.pyplot as plt

# ==========================================
# Cấu hình Thực nghiệm
# ==========================================
LOGS_DIR = 'logs/'                    # Thư mục chứa kết quả của 10 runs từ C++ (tạo ra từ Bash Script)
RESULTS_CSV = 'pilot_results.csv'      # File tổng hợp sau cùng (Trung bình + Độ lệch chuẩn)
BASELINE_CSV = 'baseline_bks.csv'      # [TO DO] File chứa kết quả baseline để làm Wilcoxon test

# Khởi tạo DataFrame lưu trữ kết quả
columns = ['Instance', 'Group', 'Runs', 
           'HV_Mean', 'HV_Std', 'HV_Min', 'HV_Max',
           'IGD_Mean', 'IGD_Std',
           'Pareto_Mean', 'Pareto_Std',
           'Runtime_Mean', 'Runtime_Std',
           'Best_Vehicles', 'Best_Distance', 'Feasibility_Percent']
df_results = pd.DataFrame(columns=columns)

# Tính nhóm dựa trên tên. VD: "C101" -> "C1"
def get_group_name(instance_name):
    match = re.match(r'^([a-zA-Z]+[0-9])', instance_name)
    if match:
        return match.group(1).upper()
    return "UNKNOWN"

# ==========================================
# Hàm thu thập Metrics từ C++ Log
# ==========================================
def parse_summary_file(filepath):
    """Đọc file summary.txt để lấy thông tin HV, IGD... (Dựa vào format C++ hiện tại)"""
    data = {}
    if not os.path.exists(filepath):
        return data
        
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()
        
        # Mẫu Regex (cần điều chỉnh nếu C++ in ra khác)
        hv_match = re.search(r'Hypervolume \(HV\):\s+([\d\.]+)', content)
        igd_match = re.search(r'IGD:\s+([\d\.]+)', content)
        rt_match = re.search(r'Total Execute Time:\s+([\d\.]+)', content) # ms
        ps_match = re.search(r'Archive Size:\s+(\d+)', content)
        veh_match = re.search(r'Min Vehicles:\s+([\d\.]+)', content)
        dist_match = re.search(r'Min Distance:\s+([\d\.]+)', content)

        if hv_match: data['HV'] = float(hv_match.group(1))
        if igd_match: data['IGD'] = float(igd_match.group(1))
        if rt_match: data['Runtime'] = float(rt_match.group(1)) / 1000.0 # sang giây
        if ps_match: data['ParetoSize'] = int(ps_match.group(1))
        if veh_match: data['MinVehicles'] = float(veh_match.group(1))
        if dist_match: data['MinDistance'] = float(dist_match.group(1))
            
    return data

# ==========================================
# 1. Thu thập dữ liệu
# ==========================================
if not os.path.exists(LOGS_DIR):
    print(f"Directory {LOGS_DIR} không tồn tại. Vui lòng chạy bash script c++ trước.")
else:
    instances_data = {}

    for item in os.listdir(LOGS_DIR):
        inst_dir = os.path.join(LOGS_DIR, item)
        if os.path.isdir(inst_dir):
            instance_name = item # Tên dataset, VD: c101_21
            instances_data[instance_name] = {'HV': [], 'IGD': [], 'Runtime': [], 'ParetoSize': [], 'MinVehicles': [], 'MinDistance': []}
            
            # Đọc từng run
            for run_dir in os.listdir(inst_dir):
                if run_dir.startswith('run_'):
                    summary_path = os.path.join(inst_dir, run_dir, f"{instance_name}_summary.txt") # Tuỳ theo tên C++ xuất
                    run_metrics = parse_summary_file(summary_path)
                    
                    if run_metrics:
                        for key in run_metrics:
                            instances_data[instance_name][key].append(run_metrics[key])

    # ==========================================
    # 2. Xử lý & Tạo bảng Mean/Std
    # ==========================================
    rows = []
    for inst, metrics in instances_data.items():
        runs_count = len(metrics['HV'])
        if runs_count == 0:
            continue
            
        group = get_group_name(inst)
        
        hv_arr = np.array(metrics.get('HV', []))
        igd_arr = np.array(metrics.get('IGD', []))
        time_arr = np.array(metrics.get('Runtime', []))
        psize_arr = np.array(metrics.get('ParetoSize', []))
        
        # Best values qua tất cả các runs
        min_veh = min(metrics.get('MinVehicles', [np.nan]))
        min_dist = min(metrics.get('MinDistance', [np.nan]))
        
        feasibility = 100.0 * (runs_count / 10.0) # Assume if file exist, it's feasible
        
        rows.append({
            'Instance': inst,
            'Group': group,
            'Runs': runs_count,
            'HV_Mean': np.mean(hv_arr) if len(hv_arr)>0 else np.nan,
            'HV_Std': np.std(hv_arr, ddof=1) if len(hv_arr)>1 else 0.0,
            'HV_Min': np.min(hv_arr) if len(hv_arr)>0 else np.nan,
            'HV_Max': np.max(hv_arr) if len(hv_arr)>0 else np.nan,
            'IGD_Mean': np.mean(igd_arr) if len(igd_arr)>0 else np.nan,
            'IGD_Std': np.std(igd_arr, ddof=1) if len(igd_arr)>1 else 0.0,
            'Pareto_Mean': np.mean(psize_arr) if len(psize_arr)>0 else np.nan,
            'Pareto_Std': np.std(psize_arr, ddof=1) if len(psize_arr)>1 else 0.0,
            'Runtime_Mean': np.mean(time_arr) if len(time_arr)>0 else np.nan,
            'Runtime_Std': np.std(time_arr, ddof=1) if len(time_arr)>1 else 0.0,
            'Best_Vehicles': min_veh,
            'Best_Distance': min_dist,
            'Feasibility_Percent': feasibility
        })

    df_results = pd.DataFrame(rows)
    df_results.to_csv(RESULTS_CSV, index=False)
    print(f"Đã lưu kết quả tổng hợp vào: {RESULTS_CSV}")

    # ==========================================
    # 3. Phân tích 4 Câu hỏi báo cáo
    # ==========================================
    print("\n--- PHÂN TÍCH PILOT ---")
    
    # Q1. Độ ổn định (HV std/mean)
    df_results['HV_Stability'] = df_results['HV_Std'] / df_results['HV_Mean']
    unstable = df_results[df_results['HV_Stability'] > 0.15]
    print(f"\n1. Ổn định (Std/Mean > 15%): Có {len(unstable)} instance không ổn định.")
    if len(unstable) > 0: print(unstable[['Instance', 'HV_Mean', 'HV_Std', 'HV_Stability']])

    # Q3. Yếu điểm theo Group C/R/RC
    print("\n3. Trung bình theo Nhóm (Group Analysis):")
    group_stats = df_results.groupby('Group')[['HV_Mean', 'IGD_Mean', 'Runtime_Mean']].mean()
    print(group_stats)

    # Q4. Thời gian chạy
    print("\n4. Thời gian chạy lớn nhất:")
    max_time_inst = df_results.loc[df_results['Runtime_Mean'].idxmax()]
    print(f"  {max_time_inst['Instance']}: {max_time_inst['Runtime_Mean']:.2f}s (Std: {max_time_inst['Runtime_Std']:.2f}s)")
    if max_time_inst['Runtime_Mean'] > 300:
        print("  [!CẢNH BÁO!] Thời gian vượt quá 5 phút.")

    # Q2. Wilcoxon Test (Mẫu code, cần dữ liệu baseline thực tế để chạy)
    print("\n2. Wilcoxon Test (ALNS vs Baseline): Chờ dữ liệu baseline (NSGA-II) tại `baseline_bks.csv` để đối chiếu từng Array kết quả.")
