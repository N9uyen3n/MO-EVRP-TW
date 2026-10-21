"""Read-only analysis of isolated S1 traces and completed RC205 split audits."""
from collections import Counter
import csv
import json
from pathlib import Path
import statistics
import sys

from freeze_smd_stage import sha


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def read_nodes(path):
    return [dict(kind=f[1],x=float(f[2]),y=float(f[3]))
            for line in path.read_text(encoding="utf-8-sig").splitlines()
            if len(f:=line.split())==8 and f[1] in ("d","c","f")]


def centroids(event,nodes):
    result=[]
    for route in event["routes"]:
        customers=[nodes[n] for n in route if nodes[n]["kind"]=="c"]
        x=sum(n["x"] for n in customers)/len(customers) if customers else 0
        y=sum(n["y"] for n in customers)/len(customers) if customers else 0
        result.append((x,y) if x or y else (nodes[0]["x"],nodes[0]["y"]))
    return result


def distances(row,r,centers):
    x,y=centers[r]
    return [(centers[q][0]-x)**2+(centers[q][1]-y)**2 for q in row if q!=r]


def trace_analysis(root,stages):
    manifest=json.loads((root/"manifest.json").read_text())
    require(manifest["status"]=="completed","Incomplete trace")
    require(sha(root/"trace_smd_scope.py")==manifest["tool_sha256"],"Trace tool changed")
    for stage,record in manifest["stages"].items():
        require(sha(stages/stage/"source.zip")==record["source_zip_sha256"],"Frozen source changed")
        require(sha(root/stage/"build/ALNSAuditRunner")==record["binary_sha256"],"Trace executable changed")
        for name,digest in record["modified_source_sha256"].items():
            require(sha(root/stage/"source"/name)==digest,"Instrumented source changed")
    for run in manifest["runs"]:
        folder=root/run["stage"]/run["case"]
        require(sha(folder/"smd_trace.jsonl")==run["trace_sha256"],"Trace changed")
        require(sha(stages/run["stage"]/f"{run['case']}.txt")==run["input_sha256"],"Trace input changed")
        for name,digest in run["csv_sha256"].items():
            require(sha(folder/"audit"/name)==digest,"Trace output changed")
        require(json.loads((folder/"independent_validation.json").read_text())==run["validation"],"Validation changed")
        require(run["validation"]["invalid"]==run["validation"]["errors"]==0,"Invalid trace output")
        require(not (folder/"audit/validation.txt").read_text().strip(),"C++ diagnostic validation errors")
    results={}
    for case,comparison in manifest["comparisons"].items():
        nodes=read_nodes(stages/"B0"/f"{case}.txt")
        customers={i for i,n in enumerate(nodes) if n["kind"]=="c"}
        traces={stage:[json.loads(line) for line in (root/stage/case/"smd_trace.jsonl").read_text().splitlines()]
                for stage in ("B0","B1")}
        # Derived inputs for fresh-route replay of internal candidates. Replaying
        # these orders diagnoses rejections, not validity of the raw schedules.
        with (root/f"replay_sequences_{case}.txt").open("w") as replay:
            for stage,events in traces.items():
                for event_index,event in enumerate(events):
                    if event["phase"] not in ("before_ls","after_ls","after_intensified_ls","reject_post_ls","vnd_accepted"):
                        continue
                    replay.write(f"{event_index} {stage} {event['iteration']} {event['phase']} {event['op']} {len(event['routes'])}\n")
                    for route in event["routes"]:
                        replay.write(f"{len(route)} "+" ".join(map(str,route))+"\n")
        require(traces["B0"][0]==traces["B1"][0],"Different initial solution")
        index=next(i for i,(a,b) in enumerate(zip(traces["B0"],traces["B1"])) if a!=b)
        require(index==comparison["common_events"],"First divergence changed")
        first=traces["B0"][index]
        centers=centroids(first,nodes)
        examples=[]
        for row in comparison["first_difference"]["neighbor_differences"]:
            examples.append(dict(row, before_squared_distances=distances(row["before"],row["route"],centers),
                                 after_squared_distances=distances(row["after"],row["route"],centers)))
        result=dict(first_difference=comparison["first_difference"],first_neighbor_examples=examples,
                    first_route_difference=comparison["first_route_difference"],stages={})
        first_nodes=next((i for i,(a,b) in enumerate(zip(traces["B0"],traces["B1"])) if a["routes"]!=b["routes"]),None)
        require(first_nodes==comparison["first_route_difference"]["event"],"Trace event alignment changed before first route divergence")
        for stage,events in traces.items():
            internal_coverage_failures=[]; strict_missing=0; first_strict=None
            oracle_mismatches=0; context_count=0
            for event_index,event in enumerate(events):
                counts=Counter(n for route in event["routes"] for n in route if n in customers)
                missing=sorted(customers-counts.keys()); duplicates=sorted(n for n,c in counts.items() if c!=1)
                if (missing or duplicates) and event["phase"] in (
                    "before_ls","after_ls","after_intensified_ls","reject_post_ls","vnd_accepted"):
                    internal_coverage_failures.append(dict(event=event_index,iteration=event["iteration"],
                        phase=event["phase"],op=event["op"],missing=missing,duplicates=duplicates))
                if event["phase"] not in ("vnd_start","vnd_patch"):
                    continue
                context_count+=1
                require(len(event["neighbors"])==len(event["oracle_neighbors"]),"Oracle size mismatch")
                oracle_mismatches+=event["neighbors"]!=event["oracle_neighbors"]
                centers=centroids(event,nodes)
                for r,row in enumerate(event["neighbors"]):
                    omitted=set(event["oracle_neighbors"][r])-set(row)
                    present=distances(row,r,centers)
                    if omitted and present and min(distances(sorted(omitted),r,centers)) < max(present)-1e-8:
                        strict_missing+=1
                        if first_strict is None:
                            first_strict=dict(event=event_index,iteration=event["iteration"],phase=event["phase"],
                                             route=r,neighbors=row,oracle=event["oracle_neighbors"][r])
            if stage=="B1":
                require(oracle_mismatches==0,"B1 context disagrees with oracle")
            result["stages"][stage]=dict(context_snapshots=context_count,oracle_mismatching_snapshots=oracle_mismatches,
                strictly_closer_omitted_rows=strict_missing,first_strict_omission=first_strict,
                internal_coverage_failures=internal_coverage_failures,
                post_ls_rejections=[dict(event=i,iteration=e["iteration"]) for i,e in enumerate(events)
                                    if e["phase"]=="reject_post_ls"])
        results[case]=result
    return dict(manifest_sha256=sha(root/"manifest.json"),comparisons=results,
                solutions=sum(r["validation"]["solutions"] for r in manifest["runs"]),
                routes=sum(r["validation"]["routes"] for r in manifest["runs"]),
                note="Diagnostic only; configuration maxIterations=25000, separate early stop after 8 iterations. "
                     "Strict omissions use squared-distance tolerance 1e-8; tie-order differences are separate.")


def split_analysis(root,report):
    saved=json.loads(report.read_text()); manifest=json.loads((root/"manifest.json").read_text())
    require(manifest["status"]=="completed" and len(manifest["runs"])==8 and len(manifest["pairs"])==4,"Incomplete split")
    require(saved["manifest_sha256"]==sha(root/"manifest.json"),"Split report no longer matches manifest")
    require(saved["overall"]["median_wall_reduction_percent"]==statistics.median(p["reduction_percent"] for p in manifest["pairs"]),
            "Split timing summary changed")
    require(saved["total_wall_seconds"]=={mode:sum(r["wall_seconds"] for r in manifest["runs"] if r["mode"]==mode)
                                          for mode in ("before","after")},"Split wall totals changed")
    require(all(p["identical_objectives"] and p["byte_identical_schedules"] for p in manifest["pairs"]),"Split behavior changed")
    for mode in ("before","after"):
        for name,digest in manifest["stages"][mode]["files"].items():
            require(sha(root/mode/name)==digest,"Split frozen stage changed")
    for run in manifest["runs"]+manifest["warmups"]:
        folder=root/run["output"]
        for name,digest in run["csv_sha256"].items():
            require(sha(folder/name)==digest,"Split CSV changed")
        require(sha(folder/"independent_validation.json")==run["validation_sha256"],"Split validation changed")
        require(run["invalid"]==0 and not (folder/"validation.txt").read_text().strip(),"Invalid split output")
    for pair in manifest["pairs"]:
        prefix=f"{pair['case']}_s{pair['seed']}_rep{pair['repetition']}_"
        for name in ("ejection_undo.json","reconstruction_cache.json"):
            require(sha(root/(prefix+"before")/name)==sha(root/(prefix+"after")/name),"Different deterministic work")
    return {key:saved[key] for key in ("stages","overall","solutions","routes","total_wall_seconds","manifest_sha256")}


def replay_analysis(root):
    reports={}
    for case in ("r101_21","rc106_21"):
        path=root/f"replay_results_{case}.csv"
        with path.open() as stream:
            rows=list(csv.DictReader(stream))
        reports[case]=dict(snapshots=len(rows),infeasible_fresh_sequence_snapshots=sum(int(r["invalid_routes"])>0 for r in rows),
            post_ls_rejections=[r for r in rows if r["phase"]=="reject_post_ls"],
            first_infeasible_by_stage={stage:next((r for r in rows if r["stage"]==stage and int(r["invalid_routes"])>0),None)
                                       for stage in ("B0","B1")},
            files_sha256={name:sha(root/name) for name in (f"replay_sequences_{case}.txt",f"replay_results_{case}.csv",f"replay_details_{case}.txt")})
    return dict(probe_source_sha256=sha(Path("src/test/probes/SMDRouteReplayProbe.cpp")),
                executable_sha256=sha(root/"route_replay"),
                linked_library_sha256=sha(Path("build-audit-smd-s3/libalns_lib.a")),cases=reports,
                note="Fresh canonical re-evaluation diagnoses internal candidates; it does not certify the original schedules. "
                     "A later VND snapshot may inherit an infeasible route from the preceding charging/VR phase.")


if __name__=="__main__":
    stages=Path("artifacts/alns_smd/20261021")
    result=dict(valid=True,split_experiments={
        stage:split_analysis(stages/f"S5_rc205_{stage.upper()}_split",Path(f"update/alns_smd_rc205_{stage}_split_results.json"))
        for stage in ("s2","s3")},
        scope_trace=trace_analysis(Path("build-audit-smd-scope-trace"),stages),
        fresh_sequence_replay=replay_analysis(Path("build-audit-smd-scope-trace")))
    destination=Path(sys.argv[1] if len(sys.argv)>1 else "update/alns_smd_followup_results.json")
    destination.write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
    print(json.dumps({"valid":True,"trace_solutions":result["scope_trace"]["solutions"],
                      "trace_routes":result["scope_trace"]["routes"],
                      "cases":{case:{stage:{key:len(value) if isinstance(value,list) else value
                                for key,value in details.items() if key!="first_strict_omission"}
                                for stage,details in comparison["stages"].items()}
                               for case,comparison in result["scope_trace"]["comparisons"].items()}},indent=2))
