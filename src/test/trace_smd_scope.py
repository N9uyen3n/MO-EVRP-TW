"""Trace the first S1 divergence in isolated B0/B1 source copies, not production.

This diagnostic preserves maxIterations=25000 but stops after a separate small
iteration limit. Trace builds are NOT timing measurements or quality benchmarks.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import zipfile

from freeze_smd_stage import sha
from run_smd_audit import dump, require
from validate_audit_schedule import validate


HEADER = r'''#pragma once
#include "core/Solution.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <utility>
#include <vector>
namespace smd_trace {
inline int iteration=-1;
inline void record(const char* phase, const Solution& solution, const Instance& instance,
                   const std::vector<std::vector<int>>* neighbors=nullptr, int op=-1) {
  static std::ofstream file("smd_trace.jsonl");
  static int events=0;
  if (++events>50000) throw std::runtime_error("SMD diagnostic event limit exceeded");
  file<<std::setprecision(17)<<"{\"iteration\":"<<iteration
      <<",\"phase\":\""<<phase<<"\",\"op\":"<<op<<",\"routes\":[";
  const auto& routes=solution.getRoutes();
  for (std::size_t r=0;r<routes.size();++r) {
    if (r) file<<',';
    file<<'[';
    const auto& nodes=routes[r].getNodes();
    for (std::size_t p=0;p<nodes.size();++p) { if(p)file<<','; file<<nodes[p]; }
    file<<']';
  }
  file<<']';
  auto lists=[&](const char* name,const std::vector<std::vector<int>>& rows) {
    file<<",\""<<name<<"\":[";
    for(std::size_t r=0;r<rows.size();++r) {
      if(r)file<<','; file<<'[';
      for(std::size_t j=0;j<rows[r].size();++j) { if(j)file<<','; file<<rows[r][j]; }
      file<<']';
    }
    file<<']';
  };
  if (neighbors) {
    lists("neighbors",*neighbors);
    // Independent full sort; never calls or changes the search context.
    std::vector<std::pair<double,double>> centroids;
    for (const auto& route:routes) {
      double x=route.getCentroidX(), y=route.getCentroidY();
      if(x==0 && y==0) { x=instance.getNodeRaw(0)->getX(); y=instance.getNodeRaw(0)->getY(); }
      centroids.emplace_back(x,y);
    }
    std::vector<std::vector<int>> oracle(routes.size());
    for(std::size_t r=0;r<routes.size();++r) {
      std::vector<std::pair<double,int>> ranked;
      for(std::size_t q=0;q<routes.size();++q) if(r!=q) {
        const double dx=centroids[r].first-centroids[q].first;
        const double dy=centroids[r].second-centroids[q].second;
        ranked.emplace_back(std::fma(dx,dx,dy*dy),static_cast<int>(q));
      }
      std::sort(ranked.begin(),ranked.end());
      oracle[r].push_back(static_cast<int>(r));
      for(std::size_t j=0;j<std::min<std::size_t>(8,ranked.size());++j)
        oracle[r].push_back(ranked[j].second);
    }
    lists("oracle_neighbors",oracle);
  }
  file<<"}\n";
  if (!file) throw std::runtime_error("Cannot write SMD diagnostic trace");
}
}
'''


def replace_once(source, old, new):
    require(source.count(old) == 1, f"Expected exactly one diagnostic anchor: {old[:100]!r}")
    return source.replace(old, new)


def instrument(source, limit):
    (source/"src/test/SMDTrace.h").write_text(HEADER, encoding="utf-8")
    search = source/"src/alns/LocalSearch.cpp"
    text = search.read_text()
    text = replace_once(text, '#include "../../include/alns/LocalSearch.h"',
                        '#include "../../include/alns/LocalSearch.h"\n#include "../test/SMDTrace.h"')
    text = replace_once(text, "  updateSearchContext(solution);\n\n  int k = 0;",
                        '  updateSearchContext(solution);\n  smd_trace::record("vnd_start",solution,*instance,&searchContext_.neighborLists);\n\n  int k = 0;')
    text = replace_once(text, "    switch (k) {",
                        '    smd_trace::record("vnd_try",solution,*instance,&searchContext_.neighborLists,k);\n    switch (k) {')
    text = replace_once(text, "    if (improved && profiler_.data.enabled) {",
                        '    if(improved) smd_trace::record("vnd_accepted",solution,*instance,nullptr,k);\n    if (improved && profiler_.data.enabled) {')
    text = replace_once(text, "      patchSearchContext(solution);",
                        '      patchSearchContext(solution);\n      smd_trace::record("vnd_patch",solution,*instance,&searchContext_.neighborLists,k);')
    search.write_text(text, encoding="utf-8")

    solver = source/"src/alns/ALNSSolver.cpp"
    text = solver.read_text()
    # Both source revisions use the same first include and solve-loop anchors.
    first_include = next(line for line in text.splitlines() if line.startswith('#include'))
    text = replace_once(text, first_include, first_include+'\n#include "../test/SMDTrace.h"')
    text = replace_once(text, "  currentTemperature = config.startTemperature;",
                        '  smd_trace::record("initial",s_current,*instance);\n  currentTemperature = config.startTemperature;')
    anchor = "  for (int i = 0; i < config.maxIterations; ++i) {"
    text = replace_once(text, anchor, anchor+f"\n    if(i>={limit}) break; // Diagnostic only: leave config.maxIterations unchanged.\n    smd_trace::iteration=i;")
    text = replace_once(text, "    t4 = now();",
                        '    smd_trace::record("after_repair",s_new,*instance);\n    t4 = now();')
    text = replace_once(text, "        localSearch.setIterationContext(i, config.maxIterations);\n        localSearch.run(s_new);",
                        '        localSearch.setIterationContext(i, config.maxIterations);\n        smd_trace::record("before_ls",s_new,*instance);\n        localSearch.run(s_new);\n        smd_trace::record("after_ls",s_new,*instance);')
    text = replace_once(text, "    t5 = now();",
                        '    smd_trace::record("after_intensified_ls",s_new,*instance);\n    t5 = now();')
    text = replace_once(text, "    if (!s_new.isFeasible())\n      goto end_iteration;",
                        '    if (!s_new.isFeasible()) {\n      smd_trace::record("reject_post_ls",s_new,*instance);\n      goto end_iteration;\n    }')
    solver.write_text(text, encoding="utf-8")


def compare(before, after):
    require(before[0]["phase"] == after[0]["phase"] == "initial", "Missing initial snapshot")
    require(before[0] == after[0], "Different initial solutions; cannot isolate S1")
    def describe(a, b, index):
        rows=[]
        if a["routes"] == b["routes"] and "neighbors" in a and "neighbors" in b:
            require(a["oracle_neighbors"] == b["oracle_neighbors"], "Different independent oracle")
            for r,(x,y) in enumerate(zip(a["neighbors"],b["neighbors"])):
                if x != y:
                    rows.append(dict(route=r,before=x,after=y,oracle=a["oracle_neighbors"][r],
                                     same_members=sorted(x)==sorted(y)))
        return dict(event=index,before_phase=a["phase"],after_phase=b["phase"],
                    before_iteration=a["iteration"],after_iteration=b["iteration"],
                    before_op=a["op"],after_op=b["op"],same_routes=a["routes"]==b["routes"],
                    neighbor_differences=rows)
    common=0
    for a,b in zip(before,after):
        if a != b: break
        common+=1
    first = describe(before[common],after[common],common) if common<min(len(before),len(after)) else None
    first_route = next((describe(a,b,index) for index,(a,b) in enumerate(zip(before,after))
                        if a["phase"]==b["phase"] and a["routes"]!=b["routes"]),None)
    def oracle_counts(events):
        checked=[e for e in events if e["phase"] in ("vnd_start","vnd_patch")]
        return dict(context_snapshots=len(checked), mismatching_rows=sum(
            row != oracle for e in checked for row,oracle in zip(e["neighbors"],e["oracle_neighbors"])),
            mismatching_snapshots=sum(e["neighbors"] != e["oracle_neighbors"] for e in checked))
    require(oracle_counts(after)["mismatching_snapshots"] == 0, "B1 differs from full oracle in real search")
    return dict(common_events=common,first_difference=first,first_route_difference=first_route,
                events_before=len(before),events_after=len(after),
                before_oracle=oracle_counts(before),after_oracle=oracle_counts(after),
                rejection_iterations={"before":[e["iteration"] for e in before if e["phase"]=="reject_post_ls"],
                                      "after":[e["iteration"] for e in after if e["phase"]=="reject_post_ls"]})


def run(args):
    root=args.output.resolve(); root.mkdir(parents=True,exist_ok=False)
    shutil.copy2(Path(__file__),root/Path(__file__).name)
    metadata=dict(status="running",diagnostic_only=True,max_iterations_config=25000,
                  diagnostic_iteration_limit=args.limit,seed=args.seed,cases=args.cases,
                  tool_sha256=sha(root/Path(__file__).name),stages={},runs=[],comparisons={})
    dump(root/"manifest.json",metadata)
    try:
        for stage in ("B0","B1"):
            frozen=args.stages/stage
            original=json.loads((frozen/"stage.json").read_text())
            require(sha(frozen/"source.zip")==original["files"]["source.zip"],"Source archive changed")
            source=root/stage/"source"; source.mkdir(parents=True)
            with zipfile.ZipFile(frozen/"source.zip") as archive: archive.extractall(source)
            instrument(source,args.limit)
            build=root/stage/"build"
            with (root/f"{stage}_build.log").open("w") as log:
                subprocess.run(["cmake","-S",str(source),"-B",str(build),"-DCMAKE_BUILD_TYPE=Release",
                                "-DALNS_INSERTION_TIME_BOUNDS=ON"],stdout=log,stderr=subprocess.STDOUT,check=True)
                subprocess.run(["cmake","--build",str(build),"--target","ALNSAuditRunner","-j",str(args.jobs)],
                               stdout=log,stderr=subprocess.STDOUT,check=True)
            metadata["stages"][stage]=dict(revision=original["revision"],source_zip_sha256=sha(frozen/"source.zip"),
                binary_sha256=sha(build/"ALNSAuditRunner"),flags_sha256=sha(build/"CMakeFiles/alns_lib.dir/flags.make"),
                modified_source_sha256={p:sha(source/p) for p in ("src/alns/LocalSearch.cpp","src/alns/ALNSSolver.cpp","src/test/SMDTrace.h")})
            dump(root/"manifest.json",metadata)
            for case in args.cases:
                input_path=frozen/f"{case}.txt"
                require(sha(input_path)==original["files"][f"{case}.txt"],"Input changed")
                folder=root/stage/case; folder.mkdir()
                output=folder/"audit"
                command=[str(build/"ALNSAuditRunner"),str(input_path.resolve()),str(output),"3600",str(args.seed),"25000","0","0","2","1","0"]
                with (folder/"run.log").open("w") as log:
                    subprocess.run(command,cwd=folder,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=600)
                validation=validate(input_path,output)
                require(validation["solutions"]>0 and validation["invalid"]==validation["errors"]==0,"Invalid diagnostic front")
                require(not (output/"validation.txt").read_text().strip(),"C++ diagnostic validation failed")
                stats=json.loads((output/"run_statistics.json").read_text())
                require(stats["iterations"]==args.limit,"Diagnostic stopped unexpectedly")
                dump(folder/"independent_validation.json",validation)
                metadata["runs"].append(dict(stage=stage,case=case,input_sha256=sha(input_path),
                    trace_sha256=sha(folder/"smd_trace.jsonl"),stats=stats,validation=validation,
                    csv_sha256={n:sha(output/n) for n in ("results.csv","schedules.csv")}))
                dump(root/"manifest.json",metadata)
                print(f"TRACED {stage}/{case}",flush=True)
        for case in args.cases:
            traces=[[json.loads(line) for line in (root/stage/case/"smd_trace.jsonl").read_text().splitlines()]
                    for stage in ("B0","B1")]
            metadata["comparisons"][case]=compare(*traces)
        metadata["status"]="completed"; dump(root/"manifest.json",metadata)
        print(json.dumps(metadata["comparisons"],indent=2),flush=True)
    except BaseException as error:
        metadata.update(status="failed",error=str(error)); dump(root/"manifest.json",metadata); raise


if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stages",type=Path,default=Path("artifacts/alns_smd/20261021"))
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--cases",nargs="+",default=["r101_21","rc106_21"])
    parser.add_argument("--seed",type=int,default=1)
    parser.add_argument("--limit",type=int,default=8)
    parser.add_argument("--jobs",type=int,default=4)
    arguments=parser.parse_args()
    require(arguments.limit>0 and arguments.jobs>0,"Positive diagnostic limits required")
    run(arguments)
