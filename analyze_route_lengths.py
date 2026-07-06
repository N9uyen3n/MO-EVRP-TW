"""
analyze_route_lengths.py
========================
Phân tích theo yêu cầu của thầy:
  - Với một instance cụ thể, chọn solution tốt nhất của FULL và No-EQ
  - Sắp xếp các route theo độ dài (TotalTime_H) tăng dần
  - So sánh phân phối độ dài route giữa 2 variant
  - Xem makespan (route dài nhất), route ngắn nhất, và time gap (max - min)
  - Kiểm tra gap có lệch nhiều không

Dữ liệu nguồn:
  - artifacts/route_workload_raw.csv  (per-route workload data)
  - artifacts/solutions_clean.csv     (objectives của từng solution)
"""

import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
import os

ARTIFACT_DIR = "artifacts"
OUTPUT_DIR = "artifacts"

# ============================================================
# THAY ĐỔI INSTANCE Ở ĐÂY NẾU MUỐN PHÂN TÍCH INSTANCE KHÁC
# ============================================================
TARGET_INSTANCES = ["c101_21", "r101_21", "rc101_21"]
# ============================================================


def pick_best_solution(routes_df: pd.DataFrame, sol_df: pd.DataFrame,
                       instance: str, variant: str) -> tuple:
    """
    Chọn solution tốt nhất theo thứ tự ưu tiên Z1 -> Z2 (distance-focus).
    Vì SolutionID trong route_workload_raw là integer (index 1..N trong Pareto front
    của từng seed), ta cần chọn theo Z1 tối thiểu rồi Z2 tối thiểu trên solutions_clean,
    sau đó map ngược lại vào route data theo (Seed, SolutionID index).

    Trả về: (seed, sol_id_int, sol_info_series, routes_dataframe)
    """
    # Lấy các seed hiện có trong route data cho (instance, variant) này
    route_seeds = routes_df[(routes_df["Instance"] == instance) & 
                            (routes_df["Variant"] == variant)]["Seed"].unique()
    
    # Lấy solutions của instance + variant và chỉ giữ lại các seed có trong route data
    sol_sub = sol_df[(sol_df["Instance"] == instance) & 
                     (sol_df["Variant"] == variant) & 
                     (sol_df["Seed"].isin(route_seeds))].copy()
    
    if sol_sub.empty:
        print(f"  [WARN] Không tìm thấy solutions (hoặc route data tương ứng) cho {instance} / {variant}")
        return None, None, None, None

    # Tìm best Z1 -> best Z2 trong đó
    best_z1 = sol_sub["Z1"].min()
    candidates = sol_sub[sol_sub["Z1"] == best_z1]
    best_sol = candidates.loc[candidates["Z2"].idxmin()]

    # Parse seed và index từ SolutionID dạng "{inst}_s{seed}_{idx}"
    sol_id_str = best_sol["SolutionID"]  # e.g. "c101_21_s7_21"
    import re
    m = re.search(r"_s(\d+)_(\d+)$", sol_id_str)
    if not m:
        print(f"  [WARN] Không parse được SolutionID: {sol_id_str}")
        return None, None, None, None

    seed = int(m.group(1))
    sol_idx = int(m.group(2))  # 0-based index trong Pareto front

    # SolutionID trong route_workload_raw là 1-based, tương ứng với thứ tự của
    # nghiệm trong Pareto front. Kiểm tra xem có bao nhiêu SolutionID trong route data.
    route_sub = routes_df[
        (routes_df["Instance"] == instance) &
        (routes_df["Variant"] == variant) &
        (routes_df["Seed"] == seed)
    ]

    available_sol_ids = sorted(route_sub["SolutionID"].unique())
    # SolutionID trong route data đánh từ 1..N (N = số nghiệm trong Pareto front của seed đó)
    # sol_idx (từ solutions_clean) là 0-based index
    # Nếu sol_idx+1 có trong route data thì dùng, nếu không thì chọn SolutionID gần nhất
    target_sol_id = sol_idx + 1  # convert 0-based -> 1-based
    if target_sol_id not in available_sol_ids:
        # Fallback: chọn SolutionID gần nhất có trong route data
        # (có thể route data chỉ lưu 1 solution đại diện)
        target_sol_id = available_sol_ids[0]

    routes = route_sub[route_sub["SolutionID"] == target_sol_id].copy()
    return seed, target_sol_id, best_sol, routes


def analyze_instance(instance: str):
    print(f"\n{'='*70}")
    print(f"  PHÂN TÍCH INSTANCE: {instance}")
    print(f"{'='*70}")

    routes_df = pd.read_csv(f"{ARTIFACT_DIR}/route_workload_raw.csv")
    sol_df = pd.read_csv(f"{ARTIFACT_DIR}/solutions_clean.csv")

    results = {}
    for variant in ["FULL", "No-EQ"]:
        print(f"\n--- Variant: {variant} ---")
        seed, sol_id, best_sol, routes = pick_best_solution(
            routes_df, sol_df, instance, variant
        )
        if routes is None:
            continue

        # Sắp xếp route theo TotalTime_H tăng dần
        routes_sorted = routes.sort_values("TotalTime_H", ascending=True).reset_index(drop=True)
        routes_sorted.index = routes_sorted.index + 1  # Route 1, 2, 3, ...

        print(f"  Solution tốt nhất: {best_sol['SolutionID']}")
        print(f"  Z1={best_sol['Z1']:.0f} xe, Z2={best_sol['Z2']:.2f}, "
              f"Z3={best_sol['Z3']:.4f}, Z4={best_sol['Z4']:.2f}")
        print(f"  Seed={seed}, SolutionID trong route data={sol_id}")
        print(f"\n  Danh sách route (sắp xếp theo độ dài tăng dần):")
        print(f"  {'#':<6} {'TravelTime':>12} {'ChargeTime':>12} {'WaitTime':>12} "
              f"{'TotalTime_H':>12} {'NumCust':>8} {'NumStn':>7}")
        print(f"  {'-'*73}")

        time_list = []
        for i, (_, row) in enumerate(routes_sorted.iterrows(), 1):
            tt = row["TotalTime_H"]
            time_list.append(tt)
            print(f"  {i:<6} {row['TravelTime']:>12.2f} {row['ChargeTime']:>12.2f} "
                  f"{row['WaitTime']:>12.2f} {tt:>12.2f} "
                  f"{row['NumCustomers']:>8.0f} {row['NumStations']:>7.0f}")

        times = np.array(time_list)
        makespan = times.max()
        min_time = times.min()
        time_gap = makespan - min_time
        mean_time = times.mean()
        std_time = times.std(ddof=0)

        print(f"\n  THỐNG KÊ:")
        print(f"  Số xe (routes): {len(times)}")
        print(f"  Route ngắn nhất : {min_time:.2f}")
        print(f"  Route dài nhất  : {makespan:.2f}  ← MAKESPAN (Z4)")
        print(f"  Time gap (max-min): {time_gap:.2f}")
        print(f"  Mean TotalTime  : {mean_time:.2f}")
        print(f"  Std TotalTime   : {std_time:.2f}")
        print(f"  CV (std/mean)   : {std_time/mean_time:.4f}")

        results[variant] = {
            "seed": seed,
            "sol_id": sol_id,
            "best_sol": best_sol,
            "routes_sorted": routes_sorted,
            "times": times,
            "makespan": makespan,
            "min_time": min_time,
            "time_gap": time_gap,
            "mean_time": mean_time,
            "std_time": std_time,
        }

    # So sánh FULL vs No-EQ
    if "FULL" in results and "No-EQ" in results:
        print(f"\n{'='*70}")
        print(f"  SO SÁNH FULL vs No-EQ")
        print(f"{'='*70}")
        f = results["FULL"]
        n = results["No-EQ"]

        print(f"\n  {'Chỉ số':<25} {'FULL':>12} {'No-EQ':>12} {'Chênh lệch':>14}")
        print(f"  {'-'*65}")
        print(f"  {'Số xe':<25} {len(f['times']):>12} {len(n['times']):>12} "
              f"{len(f['times'])-len(n['times']):>+14}")
        print(f"  {'Makespan (route dài nhất)':<25} {f['makespan']:>12.2f} {n['makespan']:>12.2f} "
              f"{f['makespan']-n['makespan']:>+14.2f}")
        print(f"  {'Route ngắn nhất':<25} {f['min_time']:>12.2f} {n['min_time']:>12.2f} "
              f"{f['min_time']-n['min_time']:>+14.2f}")
        print(f"  {'Time gap (max-min)':<25} {f['time_gap']:>12.2f} {n['time_gap']:>12.2f} "
              f"{f['time_gap']-n['time_gap']:>+14.2f}")
        print(f"  {'Mean TotalTime':<25} {f['mean_time']:>12.2f} {n['mean_time']:>12.2f} "
              f"{f['mean_time']-n['mean_time']:>+14.2f}")
        print(f"  {'Std TotalTime':<25} {f['std_time']:>12.2f} {n['std_time']:>12.2f} "
              f"{f['std_time']-n['std_time']:>+14.2f}")
        print(f"  {'CV (std/mean)':<25} {f['std_time']/f['mean_time']:>12.4f} "
              f"{n['std_time']/n['mean_time']:>12.4f} "
              f"{f['std_time']/f['mean_time'] - n['std_time']/n['mean_time']:>+14.4f}")

        gap_pct_makespan = (f['makespan'] - n['makespan']) / n['makespan'] * 100
        gap_pct_timegap = (f['time_gap'] - n['time_gap']) / (n['time_gap'] if n['time_gap'] > 0 else 1) * 100
        print(f"\n  Gap % Makespan  : {gap_pct_makespan:+.2f}%  (FULL so với No-EQ)")
        print(f"  Gap % Time gap  : {gap_pct_timegap:+.2f}%  (FULL so với No-EQ)")

        # Vẽ biểu đồ
        _plot_comparison(instance, results)

    return results


def _plot_comparison(instance: str, results: dict):
    """Vẽ biểu đồ so sánh độ dài route giữa FULL và No-EQ."""
    fig = plt.figure(figsize=(16, 10))
    fig.suptitle(f"So sánh Route Lengths: FULL vs No-EQ\nInstance: {instance}",
                 fontsize=14, fontweight="bold", y=0.98)

    gs = gridspec.GridSpec(2, 2, figure=fig, hspace=0.45, wspace=0.35)

    colors = {"FULL": "#2196F3", "No-EQ": "#FF5722"}

    # --- Plot 1: Route lengths sorted ascending (bar chart) ---
    ax1 = fig.add_subplot(gs[0, :])  # Full width top

    offset = 0
    x_ticks = []
    x_labels = []
    bar_group_width = 0.6

    for variant, res in results.items():
        times = res["times"]
        n_routes = len(times)
        xs = np.arange(n_routes) * (len(results) + 0.5) + offset
        bars = ax1.bar(xs, times, color=colors[variant], alpha=0.85,
                       label=variant, width=bar_group_width)
        # Vẽ line makespan
        ax1.axhline(res["makespan"], color=colors[variant], linestyle="--",
                    linewidth=1.2, alpha=0.6)
        ax1.axhline(res["min_time"], color=colors[variant], linestyle=":",
                    linewidth=1.0, alpha=0.5)
        offset += 1
        for i, x in enumerate(xs):
            x_ticks.append(x)
            x_labels.append(f"{variant}\nR{i+1}")

    ax1.set_title("Độ dài từng route (TotalTime_H), sắp xếp tăng dần",
                  fontsize=11, fontweight="bold")
    ax1.set_ylabel("TotalTime_H (phút)", fontsize=10)
    ax1.set_xticks(x_ticks)
    ax1.set_xticklabels(x_labels, fontsize=7, rotation=45, ha="right")
    ax1.legend(fontsize=10)
    ax1.grid(axis="y", alpha=0.3)

    # Annotation: makespan và min
    for variant, res in results.items():
        ax1.annotate(f"Makespan={res['makespan']:.1f}",
                     xy=(0, res["makespan"]), xytext=(0.01, res["makespan"] + 5),
                     textcoords=("axes fraction", "data"),
                     fontsize=8, color=colors[variant],
                     arrowprops=dict(arrowstyle="->", color=colors[variant], lw=0.8))

    # --- Plot 2: Box plot / distribution ---
    ax2 = fig.add_subplot(gs[1, 0])
    data_for_box = [res["times"] for res in results.values()]
    bp = ax2.boxplot(data_for_box, labels=list(results.keys()),
                     patch_artist=True, widths=0.5,
                     medianprops=dict(color="black", linewidth=2))
    for patch, color in zip(bp["boxes"], colors.values()):
        patch.set_facecolor(color)
        patch.set_alpha(0.7)
    ax2.set_title("Phân phối TotalTime_H\n(Box plot)", fontsize=11, fontweight="bold")
    ax2.set_ylabel("TotalTime_H (phút)", fontsize=10)
    ax2.grid(axis="y", alpha=0.3)

    # --- Plot 3: Summary metrics comparison ---
    ax3 = fig.add_subplot(gs[1, 1])
    metrics = ["Makespan\n(max)", "Min Route", "Time Gap\n(max-min)", "Mean", "Std"]
    vals_full = [
        results["FULL"]["makespan"],
        results["FULL"]["min_time"],
        results["FULL"]["time_gap"],
        results["FULL"]["mean_time"],
        results["FULL"]["std_time"],
    ]
    vals_noeq = [
        results["No-EQ"]["makespan"],
        results["No-EQ"]["min_time"],
        results["No-EQ"]["time_gap"],
        results["No-EQ"]["mean_time"],
        results["No-EQ"]["std_time"],
    ] if "No-EQ" in results else [0]*5

    x = np.arange(len(metrics))
    w = 0.35
    ax3.bar(x - w/2, vals_full, width=w, color=colors["FULL"], alpha=0.85, label="FULL")
    ax3.bar(x + w/2, vals_noeq, width=w, color=colors["No-EQ"], alpha=0.85, label="No-EQ")

    for i, (vf, vn) in enumerate(zip(vals_full, vals_noeq)):
        ax3.text(i - w/2, vf + 5, f"{vf:.0f}", ha="center", va="bottom", fontsize=8,
                 color=colors["FULL"], fontweight="bold")
        ax3.text(i + w/2, vn + 5, f"{vn:.0f}", ha="center", va="bottom", fontsize=8,
                 color=colors["No-EQ"], fontweight="bold")

    ax3.set_title("So sánh các chỉ số thời gian", fontsize=11, fontweight="bold")
    ax3.set_ylabel("Thời gian (phút)", fontsize=10)
    ax3.set_xticks(x)
    ax3.set_xticklabels(metrics, fontsize=9)
    ax3.legend(fontsize=10)
    ax3.grid(axis="y", alpha=0.3)

    out_path = f"{OUTPUT_DIR}/fig_route_length_comparison_{instance}.png"
    plt.savefig(out_path, dpi=150, bbox_inches="tight")
    plt.close()
    print(f"\n  [OK] Biểu đồ đã lưu tại: {out_path}")


if __name__ == "__main__":
    all_results = {}
    for inst in TARGET_INSTANCES:
        res = analyze_instance(inst)
        all_results[inst] = res
    print("\nDone.")
