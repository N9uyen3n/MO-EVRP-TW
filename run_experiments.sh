#!/bin/bash

# ==============================================================================
# EVRPTW ALNS Kaggle Experiment Runner (Phase 1 & Phase 2)
# ==============================================================================

# Input Arguments
MODE=${1:-"pilot"}
DATA_DIR=${2:-"./data/solomon"}
EXECUTABLE="./build/RunALNS"
RUNS_PER_INSTANCE=10

echo "====================================================="
echo " Starting EVRPTW ALNS Experiments"
echo " Mode           : ${MODE^^}"
echo " Data Directory : $DATA_DIR"
echo " Runs/Instance  : $RUNS_PER_INSTANCE"
echo "====================================================="

# Khai báo danh sách Pilot (15 instances)
declare -a PILOT_INSTANCES=(
  "c101" "c103" "c106"
  "c201" "c205"
  "r101" "r105" "r107"
  "r201" "r205"
  "rc101" "rc103" "rc105"
  "rc201" "rc202"
)

# Hàm kiểm tra xem instance có nằm trong danh sách Pilot không
function is_pilot_instance() {
  local filename=$1
  for target in "${PILOT_INSTANCES[@]}"; do
    if [[ "${filename,,}" == "${target,,}"* ]]; then
      return 0 # True
    fi
  done
  return 1 # False
}

# 1. Biên dịch C++ code trước khi chạy
if [ ! -f "$EXECUTABLE" ]; then
    echo "[INFO] Executable not found. Compiling RunALNS..."
    mkdir -p build && cd build
    cmake -DCMAKE_BUILD_TYPE=Release ..
    make RunALNS -j4
    cd ..
    if [ ! -f "$EXECUTABLE" ]; then
        echo "[FATAL] Compilation failed. Exiting."
        exit 1
    fi
fi

# 2. Tạo thư mục logs
mkdir -p logs
total_skipped=0
total_run=0

# 3. Lặp qua tất cả file txt
for filepath in "$DATA_DIR"/*.txt; do
    if [ -f "$filepath" ]; then
        filename=$(basename -- "$filepath")
        instance_name="${filename%.*}"
        
        # Nếu chế độ là Pilot, bỏ qua các file không nằm trong danh sách
        if [[ "${MODE,,}" == "pilot" ]]; then
            if ! is_pilot_instance "$instance_name"; then
                total_skipped=$((total_skipped+1))
                continue
            fi
        fi
        
        total_run=$((total_run+1))
        echo "-----------------------------------------------------"
        echo "[$total_run] Processing instance: $instance_name"
        
        for ((run=1; run<=RUNS_PER_INSTANCE; run++)); do
            seed=$(( run * 100 + 42 )) # Seed tùy biến nhưng cố định để dễ test lại
            run_name="run_${run}"
            
            echo "   -> Run $run/$RUNS_PER_INSTANCE (Seed: $seed)"
            
            # Chạy thuật toán ($filepath = Path, $run_name = Thư mục con, $seed = Random Seed)
            $EXECUTABLE "$filepath" "$run_name" "$seed" > "logs/${instance_name}_${run_name}_console.log" 2>&1
            
            if [ $? -ne 0 ]; then
                echo "      [ERROR] Run $run failed. Check logs."
            fi
        done
        echo "      [DONE] $instance_name completed $RUNS_PER_INSTANCE runs."
    fi
done

echo "====================================================="
echo " Experiments Completed!"
echo " Total Instances Processed: $total_run (Skipped: $total_skipped)"
echo " Total Runs Executed: $((total_run * RUNS_PER_INSTANCE))"
echo "====================================================="
echo " To download results from Kaggle, zip the 'logs' folder:"
echo " zip -r logs_results.zip logs/"
echo "====================================================="
