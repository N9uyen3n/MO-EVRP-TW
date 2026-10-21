"""Verify a completed SMD audit and retain all paired timing/quality outcomes."""
import argparse
import csv
import json
from pathlib import Path
import statistics

from freeze_smd_stage import sha
from run_smd_audit import require
from summarize_charging_repair_audit import hypervolume3d


def summarize(root):
    manifest = json.loads((root/"manifest.json").read_text())
    expected = len(manifest["cases"])*len(manifest["seeds"])*manifest["repetitions"]*2
    require(manifest["status"] == "completed" and len(manifest["runs"]) == expected and
            len(manifest["pairs"]) == expected//2, "Incomplete experiment")
    index = {(r["case"], r["seed"], r["repetition"], r["mode"]): r for r in manifest["runs"]}
    require(len(index) == expected, "Duplicate runs")
    for record in manifest["runs"]+manifest["warmups"]:
        folder = root/record["output"]
        for name, digest in record["csv_sha256"].items():
            require(sha(folder/name) == digest, f"Modified CSV: {folder/name}")
        require(sha(folder/"independent_validation.json") == record["validation_sha256"], "Modified validation report")
        report = json.loads((folder/"independent_validation.json").read_text())
        require(report["solutions"] == record["solutions"] > 0 and report["routes"] == record["routes"] and
                report["invalid"] == 0 and report["errors"] == 0, "Invalid output")
        require(not (folder/"validation.txt").read_text().strip(), "C++ validation errors")
    for mode in ("before", "after"):
        for name, digest in manifest["stages"][mode]["files"].items():
            require(sha(root/mode/name) == digest, f"Modified frozen stage: {mode}/{name}")
    for name, digest in manifest["tool_sha256"].items():
        require(sha(root/name) == digest, f"Modified frozen tool: {name}")
    if manifest["kind"] in ("speed", "equivalence"):
        require(all(p["identical_objectives"] and p["byte_identical_schedules"] for p in manifest["pairs"]), "Changed search")
    require(abs(hypervolume3d([(0, 0, 0)])-1.1**3) < 1e-12, "HV cube sanity")
    require(abs(hypervolume3d([(.5, .5, .5), (.7, .7, .7)])-.6**3) < 1e-12, "HV dominated point sanity")
    boxes, hypervolumes = {}, {}
    for case in manifest["cases"]:
        selected = [r for r in manifest["runs"] if r["case"] == case]
        fleet = min(int(r["best"]["vehicles"]) for r in selected)
        lines = (root/"before"/f"{case}.txt").read_text(encoding="utf-8-sig").splitlines()
        parameters = {line.strip()[0]: float(line.split("/")[1]) for line in lines if "/" in line}
        depot = next(line.split() for line in lines if len(line.split()) == 8 and line.split()[1] == "d")
        horizon = float(depot[6])-float(depot[5])
        scale = (fleet*horizon*parameters["v"], 1.0, horizon)
        boxes[case] = dict(fleet=fleet, scale=scale, reference=[1.1]*3)
        for record in selected:
            with (root/record["output"]/"results.csv").open(newline="") as stream:
                points = [(float(row["distance"])/scale[0],float(row["gini"]),float(row["max_time"])/scale[2])
                          for row in csv.DictReader(stream) if int(row["vehicles"]) == fleet]
            hypervolumes[record["output"]] = hypervolume3d(points) if points else None
    pairs = []
    for source in manifest["pairs"]:
        before, after = (index[source["case"],source["seed"],source["repetition"],mode] for mode in ("before", "after"))
        row = dict(source)
        for mode, record in (("before", before), ("after", after)):
            for metric in ("vehicles", "distance", "gini", "max_time"):
                row[f"{mode}_{metric}"] = float(record["best"][metric])
            row[f"{mode}_hv3d"] = hypervolumes[record["output"]]
            row[f"{mode}_wall_seconds"] = record["wall_seconds"]
            row[f"{mode}_iterations"] = record["stats"]["iterations"]
            row[f"{mode}_search_overshoot_ms"] = max(0,record["stats"]["search_ms"]-manifest["seconds"]*1000)
        av, bv, ad, bd = (row[k] for k in ("before_vehicles", "after_vehicles", "before_distance", "after_distance"))
        row["outcome"] = "win" if bv < av or (bv == av and bd < ad-1e-6) else (
            "loss" if bv > av or (bv == av and bd > ad+1e-6) else "tie")
        row["distance_reduction_percent"] = 100*(1-bd/ad) if av == bv else None
        ah, bh = row["before_hv3d"], row["after_hv3d"]
        row["hv_outcome"] = ("win" if bh > ah+1e-12 else "loss" if bh < ah-1e-12 else "tie") if ah is not None and bh is not None else "incomparable"
        pairs.append(row)
    def aggregate(selected):
        return dict(pairs=len(selected), faster=sum(p["reduction_percent"] > 0 for p in selected),
                    median_wall_reduction_percent=statistics.median(p["reduction_percent"] for p in selected),
                    minimum_wall_reduction_percent=min(p["reduction_percent"] for p in selected),
                    maximum_wall_reduction_percent=max(p["reduction_percent"] for p in selected),
                    quality={outcome: sum(p["outcome"] == outcome for p in selected) for outcome in ("win", "tie", "loss")},
                    hv={outcome: sum(p["hv_outcome"] == outcome for p in selected) for outcome in ("win", "tie", "loss", "incomparable")})
    counters = {}
    for mode in ("before", "after"):
        records = [r["or_opt_cache"] for r in manifest["runs"] if r["mode"] == mode and "or_opt_cache" in r]
        if records:
            counters[mode] = {key: (max(r.get(key,0) for r in records) if key in ("peak_entries", "capacity", "bytes")
                                    else sum(r.get(key,0) for r in records))
                              for key in records[0] if key != "mode"}
    report = dict(kind=manifest["kind"], source_manifest=str(root/"manifest.json"), manifest_sha256=sha(root/"manifest.json"),
                  stages={mode: manifest["stages"][mode]["revision"] for mode in ("before", "after")},
                  mode_settings=manifest["mode_settings"], runs=expected,
                  solutions=sum(r["solutions"] for r in manifest["runs"]), routes=sum(r["routes"] for r in manifest["runs"]), invalid=0,
                  warmup_solutions=sum(r["solutions"] for r in manifest["warmups"]), warmup_routes=sum(r["routes"] for r in manifest["warmups"]),
                  identical_pairs=sum(p["identical_objectives"] and p["byte_identical_schedules"] for p in pairs),
                  overall=aggregate(pairs), by_case={case: aggregate([p for p in pairs if p["case"] == case]) for case in manifest["cases"]},
                  total_wall_seconds={mode: sum(r["wall_seconds"] for r in manifest["runs"] if r["mode"] == mode) for mode in ("before", "after")},
                  maximum_search_overshoot_ms=max(p[f"{mode}_search_overshoot_ms"] for p in pairs for mode in ("before", "after")),
                  hv_boxes=boxes, or_opt_cache=counters, pairs=pairs,
                  summary_tool_sha256={name: sha(Path(__file__).with_name(name)) for name in
                                       (Path(__file__).name, "summarize_charging_repair_audit.py")})
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    report = summarize(args.root)
    args.destination.write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
    with args.destination.with_suffix(".csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(report["pairs"][0]))
        writer.writeheader(); writer.writerows(report["pairs"])
    print(json.dumps({key: value for key,value in report.items() if key not in ("pairs", "summary_tool_sha256")}, indent=2))
