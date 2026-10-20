"""Summarize all paired runs; compare vehicle counts before other objectives."""
import csv
import json
import math
from pathlib import Path
import statistics
import sys


def hypervolume3d(points,reference=(1.1,1.1,1.1)):
    points=sorted(set(tuple(p) for p in points if all(a<b for a,b in zip(p,reference))))
    volume=0.0
    xs=sorted({p[0] for p in points})+[reference[0]]
    for left,right in zip(xs,xs[1:]):
        active=[p for p in points if p[0]<=left]
        ys=sorted({p[1] for p in active})+[reference[1]]
        area=0.0
        lowest=reference[2]
        for lower,upper in zip(ys,ys[1:]):
            lowest=min(lowest,*(p[2] for p in active if p[1]==lower))
            area+=(upper-lower)*(reference[2]-lowest)
        volume+=(right-left)*area
    return volume


def main(folder,destination):
    manifest=json.loads((folder/'manifest.json').read_text())
    if manifest['status']!='completed': raise RuntimeError('Experiment is incomplete')
    runs=json.loads((folder/'summary.json').read_text())
    if len(runs)!=18 or any(r['invalid'] for r in runs): raise RuntimeError('Expected 18 fully valid runs')
    index={(r['instance'],r['seed'],r['charging_repair']):r for r in runs}
    pairs=[]
    hv_by_run={}
    reference_boxes={}
    for case in manifest['cases']:
        records=[r for r in runs if r['instance']==case]
        fleet=min(int(r['best']['vehicles']) for r in records)
        lines=(folder/'instances'/f'{case}.txt').read_text().splitlines()
        depot=next(line.split() for line in lines if line.split() and line.split()[0]=='D0')
        horizon=float(depot[6])-float(depot[5])
        velocity=next(float(line.split('/')[1]) for line in lines if line.strip().startswith('v '))
        # Each feasible route spends at most horizon on travel+service+charging.
        # Consequently total distance <= fleet * horizon * velocity. These are
        # common physical normalization bounds, not per-front observed min/max.
        bounds=(fleet*horizon*velocity,1.0,horizon)
        reference_boxes[case]=dict(fleet=fleet,normalization_bounds=bounds,reference=[1.1]*3)
        for r in records:
            key=(case,r['seed'],r['charging_repair'])
            if int(r['best']['vehicles'])!=fleet:
                hv_by_run[key]=None
                continue
            root=Path(__file__).resolve().parents[2]
            results=list(csv.DictReader((root/r['directory']/'output/results.csv').open()))
            points=[(float(row['distance'])/bounds[0],float(row['gini']),float(row['max_time'])/horizon)
                    for row in results if int(row['vehicles'])==fleet]
            hv_by_run[key]=hypervolume3d(points)
        for seed in manifest['seeds']:
            off,on=index[case,seed,False],index[case,seed,True]
            b,a=off['best'],on['best']
            bv,av=int(b['vehicles']),int(a['vehicles'])
            bd,ad=float(b['distance']),float(a['distance'])
            outcome='win' if av<bv or (av==bv and ad<bd-1e-6) else (
                'loss' if av>bv or (av==bv and ad>bd+1e-6) else 'tie')
            pairs.append(dict(instance=case,seed=seed,off_vehicles=bv,on_vehicles=av,
                              off_distance=bd,on_distance=ad,
                              distance_change_percent=(ad/bd-1)*100 if av==bv else None,
                              off_gini=float(b['gini']),on_gini=float(a['gini']),
                              off_max_time=float(b['max_time']),on_max_time=float(a['max_time']),
                              off_hv3d=hv_by_run[case,seed,False],on_hv3d=hv_by_run[case,seed,True],
                              outcome=outcome))
    aggregate=[]
    for case in manifest['cases']:
        for enabled in [False,True]:
            subset=[r for r in runs if r['instance']==case and r['charging_repair']==enabled]
            stats=[r['statistics']['charging_repair'] for r in subset]
            aggregate.append(dict(instance=case,charging_repair=enabled,
                vehicles=[int(r['best']['vehicles']) for r in subset],
                distances=[float(r['best']['distance']) for r in subset],
                median_process_seconds=statistics.median(r['process_wall_seconds'] for r in subset),
                maximum_search_overshoot_ms=max(max(0,r['statistics']['search_ms']-manifest['seconds_per_run']*1000) for r in subset),
                calls=sum(s['calls'] for s in stats),feasible=sum(s['feasible'] for s in stats),
                exhausted=sum(s['exhausted'] for s in stats),evaluations=sum(s['evaluations'] for s in stats),
                repair_seconds=sum(s['elapsed_us'] for s in stats)/1e6,
                relocate_accepted=sum(s['relocate_accepted'] for s in stats),
                inter_two_opt_accepted=sum(s['inter_two_opt_accepted'] for s in stats),
                candidate_vehicle_reductions=sum(s['vehicles_reduced'] for s in stats)))
    result=dict(experiment=str(folder),runs=len(runs),
                validated_solutions=sum(r['validated_solutions'] for r in runs),
                validated_routes=sum(r['validated_routes'] for r in runs),invalid=0,
                wins=sum(p['outcome']=='win' for p in pairs),losses=sum(p['outcome']=='loss' for p in pairs),
                ties=sum(p['outcome']=='tie' for p in pairs),pairs=pairs,aggregate=aggregate,
                hv_reference_boxes=reference_boxes,
                note='Representative selects fewest vehicles then distance. HV is compared at the common best fleet only; None means higher fleet. Candidate vehicle reduction counters are not final fleet improvement over baseline.')
    with destination.open('x',encoding='utf-8') as handle: json.dump(result,handle,indent=2)
    with destination.with_suffix('.csv').open('x',newline='') as handle:
        writer=csv.DictWriter(handle,fieldnames=list(pairs[0]));writer.writeheader();writer.writerows(pairs)
    print(json.dumps({k:v for k,v in result.items() if k not in ('pairs','aggregate')},indent=2))


if __name__=='__main__':
    # Analytic unions catch mistakes in the report metric independently of ALNS.
    if not math.isclose(hypervolume3d([(0,0,0)],(1,1,1)),1): raise RuntimeError('HV origin test')
    if not math.isclose(hypervolume3d([(0.5,0.5,0.5)],(1,1,1)),0.125): raise RuntimeError('HV single-box test')
    if not math.isclose(hypervolume3d([(0,0.5,0.5),(0.5,0,0)],(1,1,1)),0.625): raise RuntimeError('HV union test')
    main(Path(sys.argv[1]).resolve(),Path(sys.argv[2]).resolve())
