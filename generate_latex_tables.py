import os
import pandas as pd
import numpy as np
from scipy import stats

ARTIFACT_DIR = "artifacts"
OUT_DIR = os.path.join(ARTIFACT_DIR, "latex_tables")
os.makedirs(OUT_DIR, exist_ok=True)

import re

def get_series(instance_name):
    # e.g., 'c101_21' -> 'C1', 'r111_21' -> 'R1'
    if pd.isna(instance_name): return 'Unknown'
    match = re.match(r'^([a-zA-Z]+\d)', str(instance_name))
    if match:
        return match.group(1).upper()
    return 'Unknown'

def filter_21(df, col='Instance'):
    if col in df.columns:
        return df[df[col].astype(str).str.endswith('_21')].copy()
    return df

# 1. Protocol Table
print("Generating Table 1: Protocol")
protocol = pd.read_csv(f"{ARTIFACT_DIR}/table1_protocol.csv")
with open(f"{OUT_DIR}/table1_protocol.tex", "w", encoding="utf-8") as f:
    f.write("\\begin{tabular}{lc}\n\\toprule\n")
    f.write("\\textbf{Metric} & \\textbf{Value} \\\\\n\\midrule\n")
    for _, row in protocol.iterrows():
        metric = row['Metric'].replace('_', '\\_')
        f.write(f"{metric} & {row['Value']} \\\\\n")
    f.write("\\bottomrule\n\\end{tabular}\n")

# 2 & 3. Fleet Targets & Attainment
print("Generating Table 2 & 3: Fleet Attainment")
fleet_targets = pd.read_csv(f"{ARTIFACT_DIR}/fleet_targets.csv")
fleet_targets = filter_21(fleet_targets)
fleet_targets['Series'] = fleet_targets['Instance'].apply(get_series)

fleet_att = pd.read_csv(f"{ARTIFACT_DIR}/fleet_attainment.csv")
fleet_att = filter_21(fleet_att)
fleet_att['Series'] = fleet_att['Instance'].apply(get_series)

att_grouped = fleet_att.groupby(['Variant', 'Series'])['SR'].mean().unstack(level=0) * 100
with open(f"{OUT_DIR}/table3_attainment.tex", "w", encoding="utf-8") as f:
    f.write("\\begin{tabular}{lcc}\n\\toprule\n")
    f.write("\\textbf{Series} & \\textbf{FULL SR (\\%)} & \\textbf{No-EQ SR (\\%)} \\\\\n\\midrule\n")
    for series, row in att_grouped.iterrows():
        f.write(f"{series} & {row['FULL']:.2f} & {row['No-EQ']:.2f} \\\\\n")
    f.write("\\midrule\n")
    f.write(f"\\textbf{{Mean}} & \\textbf{{{att_grouped['FULL'].mean():.2f}}} & \\textbf{{{att_grouped['No-EQ'].mean():.2f}}} \\\\\n")
    f.write("\\bottomrule\n\\end{tabular}\n")

# 4. TD Solutions
print("Generating Table 4: TD Solutions")
td = pd.read_csv(f"{ARTIFACT_DIR}/td_solutions.csv")
td = filter_21(td)
td['Series'] = td['Instance'].apply(get_series)
td_grouped = td.groupby(['Variant', 'Series'])[['Z2', 'Z3', 'Z4']].mean().unstack(level=0)
with open(f"{OUT_DIR}/table4_td_solutions.tex", "w", encoding="utf-8") as f:
    f.write("\\begin{tabular}{l|cc|cc|cc}\n\\toprule\n")
    f.write("& \\multicolumn{2}{c|}{\\textbf{Distance ($Z_2$)}} & \\multicolumn{2}{c|}{\\textbf{Workload Gini ($Z_3$)}} & \\multicolumn{2}{c}{\\textbf{MaxTime ($Z_4$)}} \\\\\n")
    f.write("\\textbf{Series} & FULL & No-EQ & FULL & No-EQ & FULL & No-EQ \\\\\n\\midrule\n")
    for series, row in td_grouped.iterrows():
        f.write(f"{series} & {row[('Z2','FULL')]:.2f} & {row[('Z2','No-EQ')]:.2f} & {row[('Z3','FULL')]:.4f} & {row[('Z3','No-EQ')]:.4f} & {row[('Z4','FULL')]:.2f} & {row[('Z4','No-EQ')]:.2f} \\\\\n")
    f.write("\\bottomrule\n\\end{tabular}\n")

# 5. Hypervolume
print("Generating Table 5: Hypervolume")
hv2d = pd.read_csv(f"{ARTIFACT_DIR}/hv2d_results.csv")
hv2d = filter_21(hv2d)
hv2d['Series'] = hv2d['Instance'].apply(get_series)
hv3d = pd.read_csv(f"{ARTIFACT_DIR}/hv3d_results.csv")
hv3d = filter_21(hv3d)
hv3d['Series'] = hv3d['Instance'].apply(get_series)

hv2d_grp = hv2d.groupby(['Variant', 'Series'])['HV2D'].mean().unstack(level=0)
hv3d_grp = hv3d.groupby(['Variant', 'Series'])['HV3D'].mean().unstack(level=0)

with open(f"{OUT_DIR}/table5_hypervolume.tex", "w", encoding="utf-8") as f:
    f.write("\\begin{tabular}{l|cc|cc}\n\\toprule\n")
    f.write("& \\multicolumn{2}{c|}{\\textbf{HV2D}} & \\multicolumn{2}{c}{\\textbf{HV3D}} \\\\\n")
    f.write("\\textbf{Series} & FULL & No-EQ & FULL & No-EQ \\\\\n\\midrule\n")
    for series in hv2d_grp.index:
        f.write(f"{series} & {hv2d_grp.loc[series, 'FULL']:.4f} & {hv2d_grp.loc[series, 'No-EQ']:.4f} & {hv3d_grp.loc[series, 'FULL']:.4f} & {hv3d_grp.loc[series, 'No-EQ']:.4f} \\\\\n")
    f.write("\\bottomrule\n\\end{tabular}\n")

# 6. Wilcoxon Ablation (Recompute for _21 only)
print("Generating Table 6: Wilcoxon Ablation")
td_pivot = td.pivot(index='Instance', columns='Variant', values=['Z2', 'Z3', 'Z4']).dropna()
hv2d_pivot = hv2d.pivot(index='Instance', columns='Variant', values='HV2D').dropna()

results = []
for metric, df, is_hv in [('TD_Z2', td_pivot['Z2'], False), ('TD_Z3', td_pivot['Z3'], False), ('TD_Z4', td_pivot['Z4'], False), ('HV2D', hv2d_pivot, True)]:
    if is_hv:
        diff = df['FULL'] - df['No-EQ']
    else:
        diff = df['FULL'] - df['No-EQ']
    
    diff = diff[diff != 0]
    n = len(diff)
    if n == 0:
        continue
    
    stat, p = stats.wilcoxon(diff)
    median_diff = diff.median()
    results.append({
        'Metric': metric,
        'n': n,
        'Median Diff': median_diff,
        'p-value': p,
    })

with open(f"{OUT_DIR}/table6_ablation.tex", "w", encoding="utf-8") as f:
    f.write("\\begin{tabular}{lcccc}\n\\toprule\n")
    f.write("\\textbf{Metric} & \\textbf{n} & \\textbf{Median Diff} & \\textbf{p-value} & \\textbf{Significance ($\\alpha=0.05$)} \\\\\n\\midrule\n")
    for r in results:
        sig = "Yes" if r['p-value'] < 0.05 else "No"
        # Holm correction approx logic or just raw p-value
        metric = r['Metric'].replace('_', '\\_')
        f.write(f"{metric} & {r['n']} & {r['Median Diff']:.4f} & {r['p-value']:.2e} & {sig} \\\\\n")
    f.write("\\bottomrule\n\\end{tabular}\n")

# 7. Route Fairness
print("Generating Table 7: Route Fairness")
fairness = pd.read_csv(f"{ARTIFACT_DIR}/route_fairness_metrics.csv")
fairness = filter_21(fairness)
fairness['Series'] = fairness['Instance'].apply(get_series)

f_grouped = fairness.groupby(['Series', 'Variant']).agg(
    Min_Gini=('Gini', 'min'),
    Mean_Gini=('Gini', 'mean'),
    Median_Gini=('Gini', 'median'),
    Max_Gini=('Gini', 'max'),
    Mean_rho_max=('rho_max', 'mean'),
    Median_rho_max=('rho_max', 'median'),
    Mean_CV=('CV', 'mean'),
    Median_CV=('CV', 'median'),
    Mean_Jain=('Jain', 'mean'),
    Median_Jain=('Jain', 'median')
).reset_index()

with open(f"{OUT_DIR}/table7_route_fairness.tex", "w", encoding="utf-8") as f:
    f.write("\\begin{tabular}{llcccccccccc}\n\\toprule\n")
    f.write("\\textbf{Series} & \\textbf{Variant} & \\textbf{Min Gini} & \\textbf{Mean Gini} & \\textbf{Med Gini} & \\textbf{Max Gini} & \\textbf{Mean $\\rho_{max}$} & \\textbf{Med $\\rho_{max}$} & \\textbf{Mean CV} & \\textbf{Med CV} & \\textbf{Mean Jain} & \\textbf{Med Jain} \\\\\n\\midrule\n")
    for _, row in f_grouped.iterrows():
        f.write(f"{row['Series']} & {row['Variant']} & {row['Min_Gini']:.4f} & {row['Mean_Gini']:.4f} & {row['Median_Gini']:.4f} & {row['Max_Gini']:.4f} & {row['Mean_rho_max']:.4f} & {row['Median_rho_max']:.4f} & {row['Mean_CV']:.4f} & {row['Median_CV']:.4f} & {row['Mean_Jain']:.4f} & {row['Median_Jain']:.4f} \\\\\n")
    f.write("\\bottomrule\n\\end{tabular}\n")

# 8. Runtime Overhead
print("Generating Table 8: Runtime")
runtime = pd.read_csv(f"{ARTIFACT_DIR}/runtime_overhead.csv")
runtime = filter_21(runtime)
runtime['Series'] = runtime['Instance'].apply(get_series)
rt_grp = runtime.groupby('Series')[['FULL', 'No-EQ', 'Overhead_pct']].mean()

with open(f"{OUT_DIR}/table8_runtime.tex", "w", encoding="utf-8") as f:
    f.write("\\begin{tabular}{lccc}\n\\toprule\n")
    f.write("\\textbf{Series} & \\textbf{FULL (s)} & \\textbf{No-EQ (s)} & \\textbf{Overhead (\\%)} \\\\\n\\midrule\n")
    for series, row in rt_grp.iterrows():
        f.write(f"{series} & {row['FULL']:.2f} & {row['No-EQ']:.2f} & {row['Overhead_pct']:.2f} \\\\\n")
    f.write("\\bottomrule\n\\end{tabular}\n")

print("All LaTeX tables generated in artifacts/latex_tables/")
