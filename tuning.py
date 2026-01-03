# -*- coding: utf-8 -*-
import os
import subprocess
import itertools
import re
import argparse
import pandas as pd
from pathlib import Path
import copy

# ==============================================================================
# QUAN TRỌNG: CÀI ĐẶT THƯ VIỆN
# ==============================================================================
try:
    import pygmo as pg
except ImportError:
    print("[FATAL] Thư viện 'pygmo' chưa được cài đặt.")
    print("Vui lòng chạy: conda install -c conda-forge pygmo")
    exit(1)

# ==============================================================================
# 1. TRUNG TÂM CẤU HÌNH THAM SỐ
# ==============================================================================

DEFAULT_PARAMS = {
    "maxIterations": 25000, "segmentIterations": 100, "maxIterationsWithoutImprovement": 5000,
    "decayParameter": 0.8, "scoreDominating": 35.0, "scoreNonDominated": 15.0,
    "scoreDominated": 5.0, "scoreIdentical": 0.0, "minRemoval": 0.1, "maxRemoval": 0.4,
    "regretK": 3, "noiseParameter": 0.1, "localSearchIntensity": 30,
    "startTemperature": 150.0, "coolingRate": 0.995, "minTemperature": 0.5,
}

# --- GIAI ĐOẠN 1: Tinh chỉnh Simulated Annealing ---
PARAM_GRID = {
    "maxIterations": [2000],
    "startTemperature": [100, 150],
    "coolingRate": [0.99, 0.995],
    "minTemperature": [0.1, 1.0]
}

REFERENCE_POINT = [30, 20000, 500000, 5000]

# ==============================================================================
# 2. CÁC HÀM TIỆN ÍCH
# ==============================================================================

def parse_pareto_front(output_text):
    front = []
    matches = re.findall(
        r"Solution #\d+:\s+Veh=(\d+),\s+Dist=([\d.]+),\s+Workload=([\d.]+),\s+MaxTime=([\d.]+)",
        output_text
    )
    for match in matches:
        front.append([int(match[0]), float(match[1]), float(match[2]), float(match[3])])
    return front

def calculate_hypervolume(front, ref_point):
    if not front: return 0.0
    hv = pg.hypervolume(front)
    return hv.compute(ref_point)

def run_single_configuration(executable_path, instance_path, params, log_args=None):
    """
    Chạy một lần cấu hình.
    log_args: Một dict chứa 'outputDir' và 'runName' nếu cần ghi log.
    """
    command = [str(executable_path), str(instance_path)]
    for key, value in params.items():
        command.append(f"--{key}")
        command.append(str(value))

    # [NEW] Thêm tham số logging nếu được cung cấp
    if log_args:
        command.append(f"--outputDir")
        command.append(log_args['outputDir'])
        command.append(f"--runName")
        command.append(log_args['runName'])

    try:
        result = subprocess.run(command, capture_output=True, text=True, check=True, encoding='utf-8')
        return parse_pareto_front(result.stdout)
    except subprocess.CalledProcessError as e:
        print(f"    [ERROR] Lỗi khi chạy instance {instance_path.name} với tham số {params}")
        print(f"    Stderr: {e.stderr}")
        return []
    except FileNotFoundError:
        print(f"[FATAL] Không tìm thấy tệp thực thi: {executable_path}")
        exit(1)

# ==============================================================================
# 3. HÀM MAIN
# ==============================================================================

def main(args):
    print("========================================")
    print("      BẮT ĐẦU QUÁ TRÌNH TUNING      ")
    print(f"      Verbose Logging: {'BẬT' if args.verbose_logs else 'TẮT'}      ")
    print("========================================")

    output_path = Path(args.output_dir)
    output_path.mkdir(parents=True, exist_ok=True)

    instance_files = sorted([p for p in Path(args.data_dir).glob("*.txt") if p.is_file()])
    if not instance_files:
        print(f"[FATAL] Không tìm thấy file .txt nào trong: {args.data_dir}")
        return

    param_keys = PARAM_GRID.keys()
    param_combinations = [dict(zip(param_keys, v)) for v in itertools.product(*PARAM_GRID.values())]
    print(f"Tìm thấy {len(instance_files)} instances và {len(param_combinations)} bộ tham số để tune.")
    print(f"Tổng số lần chạy: {len(instance_files) * len(param_combinations)}\n")

    all_results = []
    for i, combination in enumerate(param_combinations):
        full_params = copy.deepcopy(DEFAULT_PARAMS)
        full_params.update(combination)

        param_str = ", ".join(f"{k}={v}" for k, v in combination.items())
        print(f"--- [{i+1}/{len(param_combinations)}] Đang xử lý: {param_str} ---")

        total_hypervolume = 0
        for instance_path in instance_files:
            # [NEW] Tạo thư mục và tên file log nếu cần
            log_args = None
            if args.verbose_logs:
                param_short_str = "-".join(f"{k[:2]}{v}" for k, v in combination.items())
                log_dir = output_path / "details" / param_short_str
                log_dir.mkdir(parents=True, exist_ok=True)
                run_name = instance_path.stem
                log_args = {"outputDir": str(log_dir), "runName": run_name}
                print(f"  -> Chạy instance: {instance_path.name} (logging to {log_dir})")
            else:
                print(f"  -> Chạy instance: {instance_path.name}")

            front = run_single_configuration(args.executable_path, instance_path, full_params, log_args)
            hv = calculate_hypervolume(front, REFERENCE_POINT)
            total_hypervolume += hv
            print(f"     Hypervolume: {hv:.4f}")

        avg_hypervolume = total_hypervolume / len(instance_files)
        print(f"  => Hypervolume trung bình: {avg_hypervolume:.4f}\n")

        result_entry = {"avg_hypervolume": avg_hypervolume, **combination}
        all_results.append(result_entry)

    results_df = pd.DataFrame(all_results)
    results_df = results_df.sort_values(by="avg_hypervolume", ascending=False)
    csv_path = output_path / "tuning_phase1_summary.csv"
    results_df.to_csv(csv_path, index=False, float_format='%.4f')

    print("\n========================================")
    print("      TUNING GIAI ĐOẠN 1 HOÀN TẤT      ")
    print("========================================")
    print(f"Báo cáo tóm tắt đã được lưu tại: {csv_path}")

    if not results_df.empty:
        best_params = results_df.iloc[0].to_dict()
        print("\n--- BỘ THAM SỐ TỐT NHẤT (Giai đoạn 1) ---")
        for key, value in best_params.items():
            print(f"  {key}: {value}")
    else:
        print("Không có kết quả nào được ghi nhận.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Tuning script cho thuật toán ALNS.")
    parser.add_argument("--data_dir", type=str, default="data/test1", help="Thư mục chứa các file instance.")
    parser.add_argument("--output_dir", type=str, default="results/tuning_phase1", help="Thư mục để lưu kết quả tuning.")
    parser.add_argument("--executable_path", type=str, default="cmake-build-release/TuningApp.exe", help="Đường dẫn đến file thực thi C++.")
    parser.add_argument('--verbose-logs', action='store_true', help='Bật log chi tiết cho mỗi lần chạy. Chú ý: Chậm và tạo rất nhiều file.')
    args = parser.parse_args()
    main(args)
