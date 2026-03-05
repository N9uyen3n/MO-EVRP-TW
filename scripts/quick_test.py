#!/usr/bin/env python3
"""
Quick Test Runner for Fairness Experiments
Chạy 1 instance với 3 modes để kiểm tra nhanh
"""

import subprocess
import sys
from pathlib import Path

# Cấu hình
BUILD_DIR = Path("cmake-build-release")
EXE_NAME = "TestALNS.exe"
TEST_INSTANCE = "..data/solomon/c101C5.txt" # Đã sửa lại đường dẫn đúng

exe_path = BUILD_DIR / EXE_NAME

if not exe_path.exists():
    print(f"❌ ERROR: {exe_path} not found!")
    print("Build the project first: cmake --build build --config Release")
    sys.exit(1)

scenarios = [
    ("COST_ONLY", "Baseline A - Cost Only"),
    ("STATIC_FAIRNESS", "Baseline B - Static Fairness"),
    ("ADAPTIVE_FAIRNESS", "Proposed - Adaptive Fairness")
]

print("=" * 60)
print("  QUICK FAIRNESS TEST")
print("=" * 60)
print(f"Instance: {TEST_INSTANCE}\n")

for mode, desc in scenarios:
    print(f"\n{'='*60}")
    print(f"  {desc} ({mode})")
    print(f"{'='*60}")
    
    output_dir = Path("results") / "quick_test" / mode
    output_dir.mkdir(parents=True, exist_ok=True)
    
    cmd = [
        str(exe_path),
        "--instance", TEST_INSTANCE,
        "--mode", mode,
        "--output", str(output_dir)
    ]
    
    try:
        result = subprocess.run(cmd, check=True, capture_output=False)
        print(f"✅ Completed: {mode}")
    except subprocess.CalledProcessError as e:
        print(f"❌ Failed: {mode} - {e}")

print("\n" + "=" * 60)
print("  ✅ QUICK TEST COMPLETED!")
print("=" * 60)
print("\nCheck results in: results/quick_test/")
