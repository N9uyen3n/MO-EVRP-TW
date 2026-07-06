"""
evrp_analysis_utils.py
=======================
Thư viện tiện ích dùng chung cho bộ notebook phân tích ablation FULL vs No-EQ
(EVRPTW-PR, MO-ALNS). Đặt file này cùng thư mục với các notebook.

QUAN TRỌNG VỀ FORMAT FILE:
Các hàm parse `_front_details.txt` và `_evolution_log.txt` được viết dựa trên
mô tả cấu trúc (dạng cây phân cấp / key-value log) chứ không dựa trên một file
mẫu thực tế. Trước khi chạy notebook 04 và 05, hãy:
  1. Mở một file `_front_details.txt` và một file `_evolution_log.txt` mẫu.
  2. Dán 20-30 dòng đầu vào biến SAMPLE_TEXT ở cuối file này (phần __main__)
     và chạy `python evrp_analysis_utils.py` để kiểm tra regex có khớp không.
  3. Chỉnh các regex trong `parse_front_details` / `parse_evolution_log`
     cho khớp định dạng thật, rồi mới chạy notebook trên toàn bộ dữ liệu.

Cấu trúc thư mục log giả định (theo tài liệu benchmark):
  <variant_dir>/<instance>/seed_<k>/<instance>_seed_<k>_{config,progress,
  operators,front_objectives,front_details,summary,evolution_log}.{csv,txt}
"""

import os
import re
import glob
import json
import hashlib
from pathlib import Path
from dataclasses import dataclass, field

import numpy as np
import pandas as pd


# ---------------------------------------------------------------------------
# 0. Khám phá cấu trúc thư mục
# ---------------------------------------------------------------------------

def discover_runs(variant_dir: str) -> pd.DataFrame:
    """Quét variant_dir (full/ hoặc no-EQ/) và liệt kê mọi (instance, seed).

    Trả về DataFrame với cột: Instance, Seed, RunDir, Prefix
    """
    rows = []
    variant_dir = Path(variant_dir)
    if not variant_dir.exists():
        return pd.DataFrame(columns=["Instance", "Seed", "RunDir", "Prefix"])

    for instance_dir in sorted(variant_dir.iterdir()):
        if not instance_dir.is_dir():
            continue
        instance = instance_dir.name
        for seed_dir in sorted(instance_dir.glob("seed_*")):
            if not seed_dir.is_dir():
                continue
            m = re.match(r"seed_(\d+)", seed_dir.name)
            seed = int(m.group(1)) if m else None
            prefix = f"{instance}_seed_{seed}"
            rows.append(
                {"Instance": instance, "Seed": seed, "RunDir": str(seed_dir), "Prefix": prefix}
            )
    return pd.DataFrame(rows)


def _file_for(run_row, suffix, ext="csv"):
    return os.path.join(run_row["RunDir"], f"{run_row['Prefix']}_{suffix}.{ext}")


# ---------------------------------------------------------------------------
# 1. _config.txt  (key: value dạng dòng, hoặc key=value)
# ---------------------------------------------------------------------------

def parse_config(path: str) -> dict:
    """Parse file config dạng key-value (key: value hoặc key=value hoặc key value)."""
    cfg = {}
    if not os.path.exists(path):
        return cfg
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            m = re.match(r"([A-Za-z_][A-Za-z0-9_]*)\s*[:=]\s*(.+)", line)
            if not m:
                # fallback: "key value" cách nhau bằng whitespace
                parts = line.split(None, 1)
                if len(parts) == 2:
                    m_key, m_val = parts
                else:
                    continue
            else:
                m_key, m_val = m.group(1), m.group(2)
            cfg[m_key.strip()] = m_val.strip()
    return cfg


def config_hash(cfg: dict, ignore_keys=("seed", "Seed", "outputDir", "OutputDir")) -> str:
    """Hash ổn định của cấu hình (bỏ qua các key phụ thuộc run như seed/output path)."""
    clean = {k: v for k, v in cfg.items() if k not in ignore_keys}
    blob = json.dumps(clean, sort_keys=True)
    return hashlib.sha1(blob.encode()).hexdigest()[:10]


# ---------------------------------------------------------------------------
# 2. _summary.txt (key: value)
# ---------------------------------------------------------------------------

def parse_summary(path: str) -> dict:
    """summary.txt cùng định dạng key-value như config.txt."""
    return parse_config(path)


def summary_numeric(summary: dict, keys=("TotalRunTime_ms", "TotalRunTime_s",
                                          "TotalIterations", "FinalArchiveSize")) -> dict:
    out = {}
    for k in keys:
        v = summary.get(k)
        if v is None:
            out[k] = np.nan
            continue
        try:
            out[k] = float(re.sub(r"[^0-9.\-]", "", v))
        except ValueError:
            out[k] = np.nan
    return out


# ---------------------------------------------------------------------------
# 3. _front_objectives.csv
# ---------------------------------------------------------------------------

def parse_front_objectives(path: str) -> pd.DataFrame:
    """Đọc file CSV Pareto front. Kỳ vọng có cột Z1..Z4 (No-EQ có thể chỉ có
    Z1,Z2,Z4 nếu equity bị loại khỏi objective vector -- kiểm tra thực tế)."""
    if not os.path.exists(path):
        return pd.DataFrame()
    df = pd.read_csv(path)
    df.columns = [c.strip() for c in df.columns]
    
    # Rename columns to standard Z1..Z4 format if they match the actual CSV output
    rename_map = {
        "TotalVehicles": "Z1",
        "TotalDistance": "Z2",
        "WorkloadGini": "Z3",
        "MaxTime": "Z4"
    }
    df = df.rename(columns=rename_map)
    return df


# ---------------------------------------------------------------------------
# 4. _progress.csv
# ---------------------------------------------------------------------------

def parse_progress(path: str) -> pd.DataFrame:
    if not os.path.exists(path):
        return pd.DataFrame()
    df = pd.read_csv(path)
    df.columns = [c.strip() for c in df.columns]
    return df


# ---------------------------------------------------------------------------
# 5. _operators.csv
# ---------------------------------------------------------------------------

def parse_operators(path: str) -> pd.DataFrame:
    if not os.path.exists(path):
        return pd.DataFrame()
    df = pd.read_csv(path)
    df.columns = [c.strip() for c in df.columns]
    return df


# ---------------------------------------------------------------------------
# 6. _front_details.txt  (cây phân cấp: nghiệm -> xe -> khách hàng/trạm sạc)
# ---------------------------------------------------------------------------

@dataclass
class RouteRecord:
    solution_id: str
    vehicle_id: str
    customers: list = field(default_factory=list)
    stations: list = field(default_factory=list)
    travel_time: float = np.nan
    service_time: float = np.nan
    charge_time: float = np.nan
    wait_time: float = np.nan
    departure_last: float = np.nan  # thời điểm hoàn thành route (proxy Makespan riêng xe)


# --- REGEX PLACEHOLDER: chỉnh lại theo file mẫu thật trước khi dùng ---
_RE_SOLUTION_HEADER = re.compile(r"^\s*==\s*Solution\s*ID\s*:\s*(\S+)", re.IGNORECASE)
_RE_VEHICLE_HEADER = re.compile(r"^\s*---\s*Route\s*ID\s*:\s*(\S+)", re.IGNORECASE)
_RE_CUSTOMER = re.compile(r"\|\s*[Cc](\d+)\s*\|\s*Customer", re.IGNORECASE)
_RE_STATION = re.compile(r"\|\s*[Ss](\d+)\s*\|\s*Station.*?\|\s*([\d.]+)\s*$", re.IGNORECASE)
_RE_TIME_FIELD = re.compile(
    r"(Total\s+Time|Travel|Service|Charge|Wait)\D*([\d.]+)", re.IGNORECASE
)
_RE_ARRIVAL_DEPARTURE = re.compile(
    r"Arrival\D*([\d.]+).*?Departure\D*([\d.]+)", re.IGNORECASE
)


def parse_front_details(path: str) -> list:
    """Parse cây phân cấp Solution -> Vehicle -> {customers, stations, times}.

    TRẢ VỀ list[RouteRecord]. Đây là parser 'best-effort' dựa trên mô tả nội
    dung file (Node xuất phát/kết thúc, danh sách khách hàng, cargo, điểm sạc
    partial recharge, timeline Arrival/Departure). HÃY kiểm tra trên file
    mẫu thật và chỉnh các regex _RE_* ở trên nếu không khớp.
    """
    records = []
    if not os.path.exists(path):
        return records

    current_solution = None
    current_route = None

    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        for raw_line in f:
            line = raw_line.rstrip("\n")
            if not line.strip():
                continue

            m_sol = _RE_SOLUTION_HEADER.match(line)
            if m_sol:
                current_solution = m_sol.group(1)
                continue

            m_veh = _RE_VEHICLE_HEADER.match(line)
            if m_veh:
                if current_route is not None:
                    records.append(current_route)
                current_route = RouteRecord(
                    solution_id=current_solution or "UNKNOWN",
                    vehicle_id=m_veh.group(1),
                )
                continue

            if current_route is None:
                continue

            for cust in _RE_CUSTOMER.finditer(line):
                current_route.customers.append(cust.group(1))

            m_stn = _RE_STATION.search(line)
            if m_stn:
                current_route.stations.append({"station": m_stn.group(1), "charge": float(m_stn.group(2))})

            for m_t in _RE_TIME_FIELD.finditer(line):
                field_name, val = m_t.group(1).lower().replace(" ", ""), float(m_t.group(2))
                if field_name == "travel" or field_name == "totaltime":
                    current_route.travel_time = val
                elif field_name == "service":
                    current_route.service_time = val
                elif field_name == "charge":
                    current_route.charge_time = val
                elif field_name == "wait":
                    current_route.wait_time = val

            m_ad = _RE_ARRIVAL_DEPARTURE.search(line)
            if m_ad:
                current_route.departure_last = float(m_ad.group(2))

    if current_route is not None:
        records.append(current_route)

    return records


def routes_to_workload_df(route_records: list) -> pd.DataFrame:
    """Chuyển list[RouteRecord] -> DataFrame active-workload A_r = travel+service+charge,
    và H_r = A_r + wait, theo mỗi (solution_id, vehicle_id)."""
    rows = []
    for r in route_records:
        travel = r.travel_time if not np.isnan(r.travel_time) else 0.0
        service = r.service_time if not np.isnan(r.service_time) else 0.0
        charge = r.charge_time if not np.isnan(r.charge_time) else 0.0
        wait = r.wait_time if not np.isnan(r.wait_time) else 0.0
        A_r = travel + service + charge
        H_r = A_r + wait
        rows.append(
            {
                "SolutionID": r.solution_id,
                "VehicleID": r.vehicle_id,
                "NumCustomers": len(r.customers),
                "NumStations": len(r.stations),
                "TravelTime": travel,
                "ServiceTime": service,
                "ChargeTime": charge,
                "WaitTime": wait,
                "ActiveWorkload_A": A_r,
                "TotalTime_H": H_r,
            }
        )
    return pd.DataFrame(rows)


# ---------------------------------------------------------------------------
# 7. _evolution_log.txt
# ---------------------------------------------------------------------------

_RE_EVOLUTION_LINE = re.compile(
    r"Iteration:\s*(\d+)\s*\|\s*Result:\s*(Accepted.*?|Rejected|Dominating).*?"
    r"Operators?:\s*(.*?)\].*?"
    r"Solution\s*\(Vehicles:\s*(\d+),\s*Distance:\s*([\d.]+)",
    re.IGNORECASE | re.DOTALL,
)


def parse_evolution_log(path: str) -> pd.DataFrame:
    """Trích các sự kiện (iteration, outcome, operator, vehicles, distance) từ
    evolution_log.txt. Regex là placeholder -- điều chỉnh theo file mẫu thật."""
    rows = []
    if not os.path.exists(path):
        return pd.DataFrame()

    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        text = f.read()

    for m in _RE_EVOLUTION_LINE.finditer(text):
        rows.append(
            {
                "Iteration": int(m.group(1)),
                "Outcome": m.group(2),
                "Operators": (m.group(3) or "").strip(),
                "Vehicles": int(m.group(4)) if m.group(4) else np.nan,
                "Distance": float(m.group(5)) if m.group(5) else np.nan,
            }
        )
    return pd.DataFrame(rows)


# ---------------------------------------------------------------------------
# 8. Manifest builder (Section 3)
# ---------------------------------------------------------------------------

def build_run_manifest(full_dir: str, noeq_dir: str) -> pd.DataFrame:
    """Xây run_manifest.csv gộp cả hai variant, mỗi dòng là 1 (variant, instance, seed)."""
    frames = []
    for variant, vdir in (("FULL", full_dir), ("No-EQ", noeq_dir)):
        runs = discover_runs(vdir)
        for _, run in runs.iterrows():
            cfg = parse_config(_file_for(run, "config", "txt"))
            summ = parse_summary(_file_for(run, "summary", "txt"))
            summ_num = summary_numeric(summ)
            fo = parse_front_objectives(_file_for(run, "front_objectives", "csv"))

            frames.append(
                {
                    "Variant": variant,
                    "Instance": run["Instance"],
                    "Seed": run["Seed"],
                    "RunDir": run["RunDir"],
                    "ConfigHash": config_hash(cfg) if cfg else None,
                    "Runtime_s": summ_num.get("TotalRunTime_s", np.nan),
                    "Iterations": summ_num.get("TotalIterations", np.nan),
                    "FinalArchiveSize_reported": summ_num.get("FinalArchiveSize", np.nan),
                    "FrontObjRows": len(fo),
                    "HasFrontObjectives": os.path.exists(_file_for(run, "front_objectives", "csv")),
                    "HasFrontDetails": os.path.exists(_file_for(run, "front_details", "txt")),
                    "HasEvolutionLog": os.path.exists(_file_for(run, "evolution_log", "txt")),
                    "HasProgress": os.path.exists(_file_for(run, "progress", "csv")),
                    "HasOperators": os.path.exists(_file_for(run, "operators", "csv")),
                }
            )
    manifest = pd.DataFrame(frames)
    if not manifest.empty:
        manifest["Valid"] = (
            manifest["HasFrontObjectives"]
            & (manifest["FrontObjRows"] > 0)
            & manifest["HasProgress"]
        )
        # Extract instance group (C/R/RC) and size heuristically from name
        manifest["Group"] = manifest["Instance"].str.extract(r"^([A-Za-z]+)")
        manifest["Group"] = manifest["Group"].str.upper().str[:2].where(
            manifest["Group"].str.upper().str.startswith("RC"), manifest["Group"].str.upper().str[0]
        )
        manifest["Size"] = manifest["Instance"].str.extract(r"(\d+)$").astype(float)
    return manifest


# ---------------------------------------------------------------------------
# 9. Pareto cleaning + non-dominance (Section 4)
# ---------------------------------------------------------------------------

def is_dominated(row, others, objective_cols, sense="min"):
    """True nếu tồn tại một điểm trong `others` thống trị `row` (minimization)."""
    dominated = False
    for _, o in others.iterrows():
        better_or_equal = all(o[c] <= row[c] for c in objective_cols)
        strictly_better = any(o[c] < row[c] for c in objective_cols)
        if better_or_equal and strictly_better:
            dominated = True
            break
    return dominated


def nondominated_filter(df: pd.DataFrame, objective_cols: list) -> pd.DataFrame:
    """Lọc tập nondominated (minimization trên tất cả objective_cols).
    Dùng thuật toán O(n^2) đơn giản -- đủ nhanh cho archive cỡ vài trăm điểm."""
    if df.empty:
        return df
    vals = df[objective_cols].to_numpy()
    n = len(df)
    keep = np.ones(n, dtype=bool)
    for i in range(n):
        if not keep[i]:
            continue
        for j in range(n):
            if i == j or not keep[j]:
                continue
            better_equal = np.all(vals[j] <= vals[i])
            strictly_better = np.any(vals[j] < vals[i])
            if better_equal and strictly_better:
                keep[i] = False
                break
    return df[keep].reset_index(drop=True)


def clean_pareto_front(df: pd.DataFrame, objective_cols: list, decimals: int = 6) -> pd.DataFrame:
    """remove infeasible (NaN) -> remove exact duplicates -> dedupe near-identical
    -> recompute nondominance."""
    if df.empty or not objective_cols:
        return df.copy()
    out = df.dropna(subset=objective_cols).copy()
    out = out.drop_duplicates(subset=objective_cols)
    rounded = out[objective_cols].round(decimals)
    out = out.loc[~rounded.duplicated()]
    out = nondominated_filter(out, objective_cols)
    return out


# ---------------------------------------------------------------------------
# 10. Common fleet target (Section 5)
# ---------------------------------------------------------------------------

def common_fleet_target(front_full: pd.DataFrame, front_noeq: pd.DataFrame,
                         seeds_full: int, seeds_noeq: int, threshold=0.5) -> int:
    """Z1_common = min k sao cho P(Z1<=... đạt k) >= threshold ở CẢ HAI variant.
    front_* cần có cột Seed, Z1. seeds_* là tổng số seed đã chạy cho variant đó.
    """
    def attain_rate(df, k, n_seeds):
        if df.empty or n_seeds == 0:
            return 0.0
        seeds_with_k = df.loc[df["Z1"] == k, "Seed"].nunique()
        return seeds_with_k / n_seeds

    candidates = sorted(set(front_full.get("Z1", pd.Series(dtype=float)).dropna().astype(int)) |
                         set(front_noeq.get("Z1", pd.Series(dtype=float)).dropna().astype(int)))
    for k in candidates:
        if attain_rate(front_full, k, seeds_full) >= threshold and \
           attain_rate(front_noeq, k, seeds_noeq) >= threshold:
            return k
    return None


def distance_focus_solution(df: pd.DataFrame, z1_target: int) -> pd.Series:
    """s^TD = argmin Z2 trong tập nghiệm có Z1 == z1_target."""
    sub = df[df["Z1"] == z1_target]
    if sub.empty:
        return None
    return sub.loc[sub["Z2"].idxmin()]


# ---------------------------------------------------------------------------
# 11. Budgeted equity curve (Section 6.2)
# ---------------------------------------------------------------------------

def budgeted_equity_curve(df: pd.DataFrame, z1_target: int, d_ref: float, h_ref: float,
                           eta_d_grid=(0.0, 0.01, 0.03, 0.05, 0.10),
                           eta_h_grid=(0.0, 0.05)) -> pd.DataFrame:
    """Với mỗi eta_D (và eta_H tệ nhất trong grid, mặc định lấy 0.05 làm ràng buộc
    maxTime nới lỏng), tìm min Z3 trong vùng feasible Z2<=(1+eta_D)*d_ref và
    Z4<=(1+eta_H)*h_ref, giới hạn Z1==z1_target."""
    rows = []
    base = df[df["Z1"] == z1_target]
    for eta_h in eta_h_grid:
        h_cap = h_ref * (1 + eta_h) if not np.isnan(h_ref) else np.inf
        for eta_d in eta_d_grid:
            d_cap = d_ref * (1 + eta_d)
            feas = base[(base["Z2"] <= d_cap) & (base["Z4"] <= h_cap)] if "Z4" in base.columns \
                else base[base["Z2"] <= d_cap]
            best_gini = feas["Z3"].min() if not feas.empty and "Z3" in feas.columns else np.nan
            rows.append({"eta_D": eta_d, "eta_H": eta_h, "d_cap": d_cap, "h_cap": h_cap,
                         "best_Gini": best_gini, "n_feasible": len(feas)})
    return pd.DataFrame(rows)


# ---------------------------------------------------------------------------
# 12. Hypervolume 2D / 3D (Section 6.3)
# ---------------------------------------------------------------------------

def normalize_objectives(df: pd.DataFrame, cols: list, ref_min=None, ref_max=None):
    ref_min = ref_min if ref_min is not None else df[cols].min()
    ref_max = ref_max if ref_max is not None else df[cols].max()
    span = (ref_max - ref_min).replace(0, 1)
    norm = (df[cols] - ref_min) / span
    return norm.clip(lower=0, upper=1), ref_min, ref_max


def hypervolume_2d(points: np.ndarray, ref_point=(1.05, 1.05)) -> float:
    """HV2D cho minimization, points đã normalize về [0,1]. Sweep theo trục x."""
    if len(points) == 0:
        return 0.0
    pts = points[np.argsort(points[:, 0])]
    hv = 0.0
    prev_x = 0.0
    # Chỉ giữ nondominated theo 2D (giả định đã lọc trước khi gọi)
    prev_y = ref_point[1]
    for x, y in pts:
        if y < prev_y:
            hv += (ref_point[0] - x) * (prev_y - y) if x < ref_point[0] else 0.0
            prev_y = y
    return hv


def hypervolume_via_montecarlo(points: np.ndarray, ref_point: np.ndarray, n_samples=200_000,
                                seed=0) -> float:
    """HV Monte-Carlo tổng quát cho 2D/3D/4D (minimization, points & ref_point
    trong không gian đã normalize [0, ref_point])."""
    if len(points) == 0:
        return 0.0
    rng = np.random.default_rng(seed)
    dim = points.shape[1]
    samples = rng.uniform(low=0.0, high=ref_point, size=(n_samples, dim))
    dominated = np.zeros(n_samples, dtype=bool)
    for p in points:
        dominated |= np.all(samples >= p, axis=1)
    box_volume = np.prod(ref_point)
    return dominated.mean() * box_volume


# ---------------------------------------------------------------------------
# 13. Route-level fairness metrics (Section 7)
# ---------------------------------------------------------------------------

def route_fairness_metrics(workload_df: pd.DataFrame, group_cols=("SolutionID",)) -> pd.DataFrame:
    """Từ DataFrame active workload per route (routes_to_workload_df), tính
    Gini, rho_max, CV, Jain index cho mỗi solution."""
    def gini(x):
        x = np.sort(np.asarray(x, dtype=float))
        n = len(x)
        if n == 0 or x.sum() == 0:
            return np.nan
        cum = np.cumsum(x)
        return (n + 1 - 2 * (cum.sum() / cum[-1])) / n

    rows = []
    for key, g in workload_df.groupby(list(group_cols)):
        A = g["ActiveWorkload_A"].to_numpy(dtype=float)
        if len(A) == 0 or A.mean() == 0:
            continue
        rho_max = A.max() / A.mean()
        cv = A.std(ddof=0) / A.mean()
        jain = (A.sum() ** 2) / (len(A) * np.sum(A ** 2)) if np.sum(A ** 2) > 0 else np.nan
        row = {
            "Gini": gini(A),
            "rho_max": rho_max,
            "CV": cv,
            "Jain": jain,
            "A_sum": A.sum(),
            "W_sum": g["WaitTime"].sum(),
            "ChargeTime_sum": g["ChargeTime"].sum(),
            "NumStations": g["NumStations"].sum(),
            "NumVehicles": len(g),
        }
        if len(group_cols) == 1:
            row[group_cols[0]] = key
        else:
            for c, v in zip(group_cols, key):
                row[c] = v
        rows.append(row)
    return pd.DataFrame(rows)


# ---------------------------------------------------------------------------
# 14. Statistics (Section 11)
# ---------------------------------------------------------------------------

def paired_wilcoxon_report(full_vals: pd.Series, noeq_vals: pd.Series):
    """Wilcoxon signed-rank, rank-biserial correlation, Hodges-Lehmann median
    difference, 95% bootstrap CI cho (FULL - No-EQ), paired theo instance."""
    from scipy import stats

    a = np.asarray(full_vals, dtype=float)
    b = np.asarray(noeq_vals, dtype=float)
    mask = ~np.isnan(a) & ~np.isnan(b)
    a, b = a[mask], b[mask]
    diff = a - b
    n = len(diff)
    if n == 0 or np.all(diff == 0):
        return {"n": n, "W": np.nan, "p": np.nan, "rank_biserial": np.nan,
                "hodges_lehmann": np.nan, "ci95_low": np.nan, "ci95_high": np.nan}

    try:
        w_stat, p_val = stats.wilcoxon(a, b, zero_method="wilcox")
    except ValueError:
        w_stat, p_val = np.nan, np.nan

    ranks = stats.rankdata(np.abs(diff))
    pos = ranks[diff > 0].sum()
    neg = ranks[diff < 0].sum()
    total = pos + neg
    rank_biserial = (pos - neg) / total if total > 0 else np.nan

    hl_pairs = np.array([(x + y) / 2 for i, x in enumerate(diff) for y in diff[i:]])
    hodges_lehmann = np.median(hl_pairs)

    rng = np.random.default_rng(0)
    boots = [np.mean(rng.choice(diff, size=n, replace=True)) for _ in range(5000)]
    ci_low, ci_high = np.percentile(boots, [2.5, 97.5])

    return {
        "n": n, "W": w_stat, "p": p_val, "rank_biserial": rank_biserial,
        "hodges_lehmann": hodges_lehmann, "ci95_low": ci_low, "ci95_high": ci_high,
    }


def holm_correction(pvalues: dict) -> dict:
    """Holm step-down correction. pvalues: {name: p}. Trả về {name: p_holm}."""
    items = sorted(pvalues.items(), key=lambda kv: (np.inf if np.isnan(kv[1]) else kv[1]))
    m = len(items)
    out = {}
    max_so_far = 0.0
    for i, (name, p) in enumerate(items):
        if np.isnan(p):
            out[name] = np.nan
            continue
        adj = min((m - i) * p, 1.0)
        max_so_far = max(max_so_far, adj)
        out[name] = max_so_far
    return out


if __name__ == "__main__":
    print("evrp_analysis_utils module OK. Chạy các notebook 01-07 để thực hiện pipeline.")
