"""Freeze a committed SMD solver stage, its build settings and audit inputs."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import shutil
import subprocess
import zipfile

CASES = ("r104_21", "r101_21", "c109_21", "rc106_21", "rc205_21")


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def freeze(revision, build, output):
    output.mkdir(parents=True, exist_ok=False)
    revision = subprocess.check_output(["git", "rev-parse", revision], text=True).strip()
    archive = subprocess.check_output(["git", "archive", "--format=zip", revision, "--",
                                       "CMakeLists.txt", "include", "src"])
    checked = 0
    with zipfile.ZipFile(io.BytesIO(archive)) as files:
        for name in files.namelist():
            production = name.startswith(("include/", "src/core/", "src/alns/", "src/io/",
                                          "src/utils/", "src/logger/", "src/app/"))
            if (production and name.endswith((".h", ".cpp"))) or name in (
                    "src/test/ALNSAuditRunner.cpp", "src/test/ScheduleValidator.h"):
                if Path(name).read_bytes().replace(b"\r\n", b"\n") != files.read(name).replace(b"\r\n", b"\n"):
                    raise RuntimeError(f"Production source differs from revision: {name}")
                checked += 1
    (output/"source.zip").write_bytes(archive)
    for filename, target in (("ALNSAuditRunner", "ALNSAuditRunner"),
                              ("CMakeCache.txt", "CMakeCache.txt"),
                              ("CMakeFiles/alns_lib.dir/flags.make", "flags.make"),
                              ("Testing/Temporary/LastTest.log", "ctest.log")):
        shutil.copy2(build/filename, output/target)
    (output/"ALNSAuditRunner").chmod(0o755)
    for case in CASES:
        shutil.copy2(Path("data/test-100")/f"{case}.txt", output/f"{case}.txt")
    manifest = dict(revision=revision, verified_production_files=checked,
                    files={p.name: sha(p) for p in sorted(output.iterdir()) if p.is_file()},
                    compiler=subprocess.check_output(["c++", "--version"], text=True),
                    scope="EVRPTW-PR delivery-only symmetric Euclidean; LS25 score25; audit CLI controls recorded per run")
    (output/"stage.json").write_text(json.dumps(manifest, indent=2)+"\n", encoding="utf-8")
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("revision")
    parser.add_argument("build", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    freeze(args.revision, args.build, args.output)
