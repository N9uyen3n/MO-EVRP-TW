import pandas as pd
import numpy as np

# Load literature data
with open('artifacts/result_q1.txt', 'r', encoding='utf-8') as f:
    code = f.read()

local_env = {}
exec(code, {}, local_env)

LITERATURE_PR = local_env['LITERATURE_PR']

# Load our Z1 results (we can use td_solutions.csv or solutions_clean.csv)
# Let's use solutions_clean.csv to get Best Z1
df_sol = pd.read_csv('artifacts/solutions_clean.csv')
df_sol = df_sol[df_sol['Instance'].astype(str).str.endswith('_21')]

# Get Best Z1 for each Instance and Variant
best_z1 = df_sol.groupby(['Instance', 'Variant'])['Z1'].min().unstack()

# Prepare comparison list
comparison = []
for instance_21 in best_z1.index:
    base_inst = instance_21.split('_')[0]
    
    bks_veh = np.nan
    bks_td = np.nan
    
    if base_inst in LITERATURE_PR and 'KC_qfree' in LITERATURE_PR[base_inst]:
        bks_veh, bks_td = LITERATURE_PR[base_inst]['KC_qfree']
        
    full_z1 = best_z1.loc[instance_21, 'FULL']
    noeq_z1 = best_z1.loc[instance_21, 'No-EQ']
    
    # Extract series
    import re
    match = re.match(r'^([a-zA-Z]+\d)', instance_21)
    series = match.group(1).upper() if match else 'Unknown'
    
    comparison.append({
        'Series': series,
        'Instance': instance_21,
        'BKS_Veh': bks_veh,
        'FULL_Z1': full_z1,
        'NoEQ_Z1': noeq_z1,
        'FULL_Gap': full_z1 - bks_veh,
        'NoEQ_Gap': noeq_z1 - bks_veh
    })

df_comp = pd.DataFrame(comparison)

# Group by Series
comp_grp = df_comp.groupby('Series').agg(
    Count=('Instance', 'count'),
    BKS_Veh=('BKS_Veh', 'mean'),
    FULL_Z1=('FULL_Z1', 'mean'),
    NoEQ_Z1=('NoEQ_Z1', 'mean'),
    FULL_Gap=('FULL_Gap', 'mean'),
    NoEQ_Gap=('NoEQ_Gap', 'mean'),
    FULL_ExactMatch=('FULL_Gap', lambda x: (x == 0).sum()),
    NoEQ_ExactMatch=('NoEQ_Gap', lambda x: (x == 0).sum())
).reset_index()

with open('artifacts/latex_tables/table2_bks_comparison.tex', 'w', encoding='utf-8') as f:
    f.write("\\begin{tabular}{lccccc}\n\\toprule\n")
    f.write("\\textbf{Series} & \\textbf{BKS $Z_1$} & \\textbf{FULL $Z_1$ (Gap)} & \\textbf{No-EQ $Z_1$ (Gap)} & \\textbf{FULL Match} & \\textbf{No-EQ Match} \\\\\n\\midrule\n")
    
    for _, row in comp_grp.iterrows():
        full_str = f"{row['FULL_Z1']:.2f} ({row['FULL_Gap']:+.2f})"
        noeq_str = f"{row['NoEQ_Z1']:.2f} ({row['NoEQ_Gap']:+.2f})"
        
        f.write(f"{row['Series']} & {row['BKS_Veh']:.2f} & {full_str} & {noeq_str} & {row['FULL_ExactMatch']}/{row['Count']} & {row['NoEQ_ExactMatch']}/{row['Count']} \\\\\n")
    
    # Overall
    overall = df_comp.mean(numeric_only=True)
    full_match = (df_comp['FULL_Gap'] == 0).sum()
    noeq_match = (df_comp['NoEQ_Gap'] == 0).sum()
    total = len(df_comp)
    
    f.write("\\midrule\n")
    full_all = f"{overall['FULL_Z1']:.2f} ({overall['FULL_Gap']:+.2f})"
    noeq_all = f"{overall['NoEQ_Z1']:.2f} ({overall['NoEQ_Gap']:+.2f})"
    f.write(f"\\textbf{{Tổng/TB}} & {overall['BKS_Veh']:.2f} & {full_all} & {noeq_all} & {full_match}/{total} & {noeq_match}/{total} \\\\\n")
    
    f.write("\\bottomrule\n\\end{tabular}\n")
    
print("Generated table2_bks_comparison.tex successfully")
