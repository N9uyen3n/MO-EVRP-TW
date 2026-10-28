"""Behavioral checks for the public experiment interface and independent verifier."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from .experiments import ROOT, load_config
from .validate import check_run

BINARY = None
FIXTURE = """StringID Type x y demand ReadyTime DueDate ServiceTime
D0 d 0 0 0 0 1000 0
S0 f 1 0 0 0 1000 0
C0 c 3 0 1 10 1000 1
Q Vehicle fuel tank capacity /10/
C Vehicle load capacity /4/
r fuel consumption rate /1/
g inverse refueling rate /1/
v average Velocity /1/
"""


class ExperimentTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="evrptw runner ")
        self.root = Path(self.temp.name)
        self.source = self.root/"instance a.txt"
        self.source.write_text(FIXTURE)
        self.config = self.root/"config.json"
        self.config.write_text(json.dumps(dict(maxIterations=12, maxTime=10000, segmentIterations=5, useLocalSearch=False)))

    def tearDown(self):
        self.temp.cleanup()

    def launch(self, *args, expected=0):
        self.assertIsNotNone(BINARY, "--binary is required for integration checks")
        proc = subprocess.run([sys.executable, str(ROOT/"run.py"), *map(str,args),
                               "--binary", str(BINARY), "--config", str(self.config)],
                              capture_output=True, text=True)
        self.assertEqual(proc.returncode,expected,proc.stdout+proc.stderr)
        return proc

    def saved_fixture(self):
        output = self.root/"saved"
        output.mkdir()
        (output/"input.txt").write_text(FIXTURE)
        (output/"run.json").write_text(json.dumps(dict(input_sha256=hashlib.sha256((output/"input.txt").read_bytes()).hexdigest())))
        (output/"results.csv").write_text("solution,vehicles,distance,gini,max_time,valid\n0,1,6,0,14,1\n")
        (output/"schedules.csv").write_text(
            "solution,route,position,node,arrival,departure,wait,battery,load,charge\n"
            "0,0,0,0,0,0,0,10,4,0\n0,0,1,2,3,11,7,7,3,0\n0,0,2,0,14,14,0,4,3,0\n")
        return output

    def test_verifier_replays_actual_waiting_and_metrics(self):
        report = check_run(self.saved_fixture())
        self.assertTrue(report["valid"],report)

    def test_verifier_rejects_station_only_route_even_when_metrics_match(self):
        output=self.saved_fixture()
        (output/"results.csv").write_text("solution,vehicles,distance,gini,max_time,valid\n0,2,8,0.2777777777777778,14,1\n")
        with (output/"schedules.csv").open("a") as handle:
            handle.write("0,1,0,0,0,0,0,10,4,0\n0,1,1,1,1,1,0,9,4,0\n0,1,2,0,2,2,0,8,4,0\n")
        report=check_run(output)
        self.assertFalse(report["valid"],report)
        self.assertTrue(any("no customer" in error for item in report["reports"] for error in item["errors"]),report)

    def test_verifier_rejects_bad_battery_without_repair(self):
        output = self.saved_fixture()
        path = output/"schedules.csv"
        path.write_text(path.read_text().replace("7,3,0", "9,3,0"))
        self.assertFalse(check_run(output)["valid"])

    def test_verifier_rejects_tampered_objectives(self):
        output = self.saved_fixture()
        path = output/"results.csv"
        path.write_text(path.read_text().replace("6,0,14", "6,0.5,14"))
        self.assertFalse(check_run(output)["valid"])

    def test_verifier_rejects_missing_customer(self):
        output = self.saved_fixture()
        path = output/"schedules.csv"
        path.write_text("\n".join(line for line in path.read_text().splitlines() if not line.startswith("0,0,1,"))+"\n")
        self.assertFalse(check_run(output)["valid"])

    def test_verifier_rejects_duplicate_solution_ids(self):
        output = self.saved_fixture()
        path = output/"results.csv"
        path.write_text(path.read_text()+"0,1,6,0,14,1\n")
        self.assertFalse(check_run(output)["valid"])

    def test_verifier_rejects_input_hash_changes(self):
        output = self.saved_fixture()
        path = output/"input.txt"
        path.write_text(path.read_text()+"\n")
        self.assertFalse(check_run(output)["valid"])

    def test_configuration_rejects_unknown_duplicate_and_invalid_values(self):
        for content in ('{"oops":1}', '{"maxTime":1,"maxTime":2}', '{"maxTime":0}',
                        '{"coolingRate":NaN}', '{"useLocalSearch":1}', '{"orOptCacheMode":3}'):
            self.config.write_text(content)
            with self.assertRaises(ValueError):
                load_config(self.config)

    def test_single_has_complete_artifacts_and_refuses_overwrite(self):
        output = self.root/"single output"
        self.launch("single","--instance",self.source,"--seed",7,"--output",output)
        metadata = json.loads((output/"run.json").read_text())
        self.assertEqual(metadata["seed"],7)
        self.assertEqual(metadata["status"],"ok")
        self.assertTrue(check_run(output)["valid"])
        for name in ("stdout.log","stderr.log","input.txt","parameters.json","parameters.cfg","run.json",
                     "results.csv","schedules.csv","cpp_validation.txt","validation.json","validation.txt",
                     "search_iterations.csv","search_evolution_log.txt","search_progress.csv","search_operators.csv",
                     "search_config.txt","search_summary.txt","search_front_objectives.csv","search_front_details.txt",
                     "run_statistics.json","local_search_profile.json","insertion_time_bounds.json",
                     "reconstruction_cache.json","ejection_undo.json","or_opt_cache.json","representative.json"):
            self.assertTrue((output/name).is_file(),name)
        with (output/"search_iterations.csv").open() as handle:
            rows = list(csv.DictReader(handle))
        stats = json.loads((output/"run_statistics.json").read_text())
        self.assertEqual(len(rows),stats["iterations"])
        self.assertEqual([int(row["iteration"]) for row in rows],list(range(stats["iterations"])))
        original=(output/"results.csv").read_bytes()
        self.launch("single","--instance",self.source,"--seed",7,"--output",output,expected=1)
        self.assertEqual((output/"results.csv").read_bytes(),original)

    def test_parallel_benchmark_runs_all_seeds_with_separate_logs(self):
        inputs = self.root/"instances"; inputs.mkdir()
        shutil.copyfile(self.source,inputs/"first.txt")
        shutil.copyfile(self.source,inputs/"second.txt")
        output = self.root/"batch"
        self.launch("benchmark","--instances-dir",inputs,"--workers",3,"--output",output)
        manifest = json.loads((output/"benchmark.json").read_text())
        self.assertEqual((manifest["status"],manifest["completed"],manifest["workers"]),("passed",20,3))
        with (output/"summary.csv").open() as handle:
            rows = list(csv.DictReader(handle))
        self.assertEqual(len(rows),20)
        for name in ("first","second"):
            for seed in range(1,11):
                folder=output/name/f"seed_{seed}"
                self.assertEqual(json.loads((folder/"run.json").read_text())["seed"],seed)
                self.assertEqual(json.loads((folder/"solver_metadata.json").read_text())["seed"],seed)
                self.assertTrue(check_run(folder)["valid"])
        single = self.root/"sequential"
        self.launch("single","--instance",inputs/"first.txt","--seed",1,"--output",single)
        for name in ("results.csv","schedules.csv"):
            self.assertEqual((single/name).read_bytes(),(output/"first/seed_1"/name).read_bytes(),name)

    def test_one_file_benchmark_with_custom_repeat_count_and_seed_range(self):
        output = self.root/"one file batch"
        self.launch("benchmark","--instance",self.source,"--workers",2,
                    "--runs",3,"--seed-start",3,"--output",output)
        manifest = json.loads((output/"benchmark.json").read_text())
        self.assertEqual((manifest["jobs"],manifest["runs_per_instance"],manifest["seed_end"]),(3,3,5))
        with (output/"summary.csv").open() as handle:
            rows = list(csv.DictReader(handle))
        self.assertEqual([int(row["seed"]) for row in rows],[3,4,5])
        self.assertTrue(all(row["status"]=="ok" for row in rows))
        for extra in (("--runs",0),("--seed-start",0),("--seed-start",4294967295,"--runs",2)):
            invalid = self.root/"invalid batch"
            self.launch("benchmark","--instance",self.source,"--workers",2,
                        "--output",invalid,*extra,expected=1)
            self.assertFalse(invalid.exists())

    def test_initialization_failure_is_reported_as_invalid(self):
        self.source.write_text(FIXTURE.replace("1 10 1000 1", "1 0 0 1"))
        output = self.root/"infeasible"
        self.launch("single","--instance",self.source,"--output",output,expected=1)
        self.assertEqual(json.loads((output/"run.json").read_text())["status"],"failed")
        self.assertFalse(json.loads((output/"validation.json").read_text())["valid"])
        self.assertTrue((output/"runner_error.txt").exists())

    def test_jd_and_unknown_formats_are_rejected_before_launch(self):
        self.source.write_text("NAME : JD\nNODE_SECTION\n0,d,0,0,0,0,0,1000,0\n")
        output = self.root/"unsupported"
        self.launch("single","--instance",self.source,"--output",output,expected=1)
        self.assertFalse(output.exists())


if __name__=="__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    args=parser.parse_args()
    BINARY=args.binary.resolve(strict=True)
    unittest.main(argv=[sys.argv[0]],verbosity=2)
