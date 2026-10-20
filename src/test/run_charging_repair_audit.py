"""Sequential paired ablation on symmetric Solomon EVRPTW-PR instances.

The same frozen Release executable runs repair off/on. A fixed-iteration
preflight checks off against the previously frozen verified baseline first.
Every returned full-precision schedule is independently replayed, without repair.
"""
import argparse
import csv
import datetime
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time

from validate_audit_schedule import validate

ROOT=Path(__file__).resolve().parents[2]
CASES=['r104_21','r101_21','c109_21']
SEEDS=[1,2,3]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write(path,value):
    temporary=path.with_suffix(path.suffix+'.tmp')
    temporary.write_text(json.dumps(value,indent=2),encoding='utf-8')
    temporary.replace(path)


def run_one(executable,instance,directory,seconds,seed,iterations,enabled,legacy=False):
    directory.mkdir(parents=True,exist_ok=False)
    output=directory/'output'
    command=[str(executable),str(instance),str(output),str(seconds),str(seed),str(iterations)]
    # Reproduce the P3-P4 comparison without later profiling/cache features.
    if not legacy: command.extend([str(int(enabled)), '0', '0', '0'])
    started=time.monotonic()
    with (directory/'console.txt').open('w') as console:
        process=subprocess.run(command,cwd=ROOT,stdout=console,stderr=subprocess.STDOUT)
    elapsed=time.monotonic()-started
    metadata=dict(command=command,exit_code=process.returncode,process_wall_seconds=elapsed)
    write(directory/'process.json',metadata)
    if process.returncode!=0:
        raise RuntimeError(f'Run failed: {directory}, exit {process.returncode}')
    validation=validate(instance,output)
    write(directory/'independent_validation.json',validation)
    if not validation['solutions'] or validation['invalid']:
        raise RuntimeError(f'Independent validation failed: {directory}')
    rows=list(csv.DictReader((output/'results.csv').open()))
    best=min(rows,key=lambda r:(int(r['vehicles']),float(r['distance'])))
    result=dict(instance=instance.stem,seed=seed,charging_repair=enabled,front_size=len(rows),
                validated_solutions=validation['solutions'],validated_routes=validation['routes'],
                invalid=validation['invalid'],best=best,process_wall_seconds=elapsed,
                statistics=json.loads((output/'run_statistics.json').read_text()),
                directory=str(directory.relative_to(ROOT)))
    write(directory/'summary.json',result)
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path)
    parser.add_argument('baseline',type=Path)
    parser.add_argument('output',type=Path)
    parser.add_argument('--seconds',type=int,default=60)
    args=parser.parse_args()
    if args.seconds<=0: raise ValueError('positive duration required')
    output=args.output.resolve(); output.mkdir(parents=True,exist_ok=False)
    executable=output/'ALNSAuditRunner'; baseline=output/'baseline_ALNSAuditRunner'
    shutil.copy2(args.executable.resolve(),executable); executable.chmod(0o755)
    shutil.copy2(args.baseline.resolve(),baseline); baseline.chmod(0o755)
    inputs=output/'instances'; inputs.mkdir()
    for case in CASES: shutil.copy2(ROOT/f'data/test-100/{case}.txt',inputs/f'{case}.txt')
    relevant=[ROOT/'CMakeLists.txt']
    for prefix in ['include','src/core','src/alns','src/io','src/logger']:
        relevant.extend(p for p in (ROOT/prefix).rglob('*') if p.suffix in ('.h','.cpp'))
    relevant.extend(ROOT/'src/test'/name for name in ['ALNSAuditRunner.cpp','ScheduleValidator.h',
                       'ChargingRepairTests.cpp','validate_audit_schedule.py','run_charging_repair_audit.py'])
    source_hashes={str(p.relative_to(ROOT)):sha(p) for p in relevant}
    manifest=dict(status='preflight',started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  model='EVRPTW-PR delivery only, symmetric Euclidean Solomon instances',
                  cases=CASES,seeds=SEEDS,seconds_per_run=args.seconds,max_iterations=25000,
                  segment_iterations=200,hv_stagnation_limit=20,local_search_intensity=25,
                  score_non_dominated=25,scatter_search=False,clock='steady/monotonic',
                  pairing='same executable off/on; alternate first mode by pair index',
                  executable_sha256=sha(executable),baseline_sha256=sha(baseline),
                  baseline_commit='646ff91ea2abcbb6a247d6b21038effd3ccc6ded',
                  input_hashes={case:sha(inputs/f'{case}.txt') for case in CASES},
                  source_hashes=source_hashes)
    write(output/'manifest.json',manifest)
    diff=subprocess.run(['git','diff','--no-ext-diff','--','CMakeLists.txt','include','src/alns',
                         'src/core','src/test/ALNSAuditRunner.cpp'],cwd=ROOT,
                        stdout=subprocess.PIPE,check=True).stdout
    (output/'tracked_source_diff.patch').write_bytes(diff)
    preflight=[]
    for case in ['c101C5','r102C10','rc108C10']:
        instance=ROOT/f'data/test1/{case}.txt'
        before=output/'preflight'/f'{case}_baseline'
        after=output/'preflight'/f'{case}_off'
        run_one(baseline,instance,before,60,1,25,False,legacy=True)
        run_one(executable,instance,after,60,1,25,False)
        def objectives(path):
            return [{k:v for k,v in row.items() if k!='elapsed_ms'}
                    for row in csv.DictReader((path/'output/results.csv').open())]
        metrics_equal=objectives(before)==objectives(after)
        schedules_equal=(before/'output/schedules.csv').read_bytes()==(after/'output/schedules.csv').read_bytes()
        preflight.append(dict(instance=case,objectives_identical=metrics_equal,schedules_identical=schedules_equal))
        write(output/'preflight.json',preflight)
        if not metrics_equal or not schedules_equal: raise RuntimeError(f'Off differs from baseline: {case}')
    print('PREFLIGHT '+json.dumps(preflight),flush=True)
    manifest['status']='running'; write(output/'manifest.json',manifest)
    reports=[]
    for pair_index,(case,seed) in enumerate((c,s) for c in CASES for s in SEEDS):
        for enabled in ([False,True] if pair_index%2==0 else [True,False]):
            label=f'{case}_seed{seed}_{"on" if enabled else "off"}'
            print('START '+label,flush=True)
            report=run_one(executable,inputs/f'{case}.txt',output/'runs'/label,
                           args.seconds,seed,25000,enabled)
            reports.append(report); write(output/'summary.json',reports)
            print('DONE '+json.dumps(report),flush=True)
    manifest.update(status='completed',completed_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
    write(output/'manifest.json',manifest)
    print('COMPLETE '+str(output),flush=True)


if __name__=='__main__':
    main()
