#!/bin/bash
# =============================================================================
# Script: run_fairness_experiments.sh
# Purpose: Automate 3 baselines for Driver Equity experiments on Linux (Colab/Kaggle)
# =============================================================================

BUILD_DIR="build"
OUTPUT_DIR="results/fairness_experiments"
DATA_DIR="data/solomon"

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN} FAIRNESS EXPERIMENT RUNNER (LINUX)${NC}"
echo -e "${CYAN}========================================${NC}"

# Check executable
EXE="$BUILD_DIR/test16" # Linux executable has no extension or .out
if [ ! -f "$EXE" ]; then
    echo -e "${RED}❌ ERROR: Executable $EXE not found${NC}"
    echo -e "${YELLOW}Please build project first: mkdir build && cd build && cmake .. && make${NC}"
    exit 1
fi

# Create output dir
mkdir -p "$OUTPUT_DIR"

# Get instances
INSTANCES=$(ls $DATA_DIR/*.txt | head -n 10) # Limit to 10 for safety
COUNT=$(echo "$INSTANCES" | wc -l)

echo -e "\n${GREEN}📁 Found $COUNT instances in $DATA_DIR${NC}"

# Define scenarios
declare -A scenarios
scenarios=( 
    ["Baseline_A_CostOnly"]="COST_ONLY"
    ["Baseline_B_StaticFairness"]="STATIC_FAIRNESS" 
    ["Proposed_AdaptiveFairness"]="ADAPTIVE_FAIRNESS"
)

# Run experiments
CURRENT_RUN=0
TOTAL_RUNS=$((3 * COUNT))

for SCENARIO_NAME in "${!scenarios[@]}"; do
    MODE="${scenarios[$SCENARIO_NAME]}"
    
    echo -e "\n\n${CYAN}╔═══════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${CYAN}║  SCENARIO: $SCENARIO_NAME ($MODE)${NC}"
    echo -e "${CYAN}╚═══════════════════════════════════════════════════════════════╝${NC}"
    
    SCENARIO_DIR="$OUTPUT_DIR/$SCENARIO_NAME"
    mkdir -p "$SCENARIO_DIR"
    
    for INSTANCE_PATH in $INSTANCES; do
        CURRENT_RUN=$((CURRENT_RUN + 1))
        INSTANCE_NAME=$(basename "$INSTANCE_PATH" .txt)
        
        echo -e "\n[${CURRENT_RUN}/${TOTAL_RUNS}] Running: $INSTANCE_NAME with $MODE"
        
        OUTPUT_SUBDIR="$SCENARIO_DIR/$INSTANCE_NAME"
        
        # Run solver
        $EXE --instance "$INSTANCE_PATH" --mode "$MODE" --output "$OUTPUT_SUBDIR"
        
        if [ $? -eq 0 ]; then
             echo -e "   ${GREEN}✅ Completed: $INSTANCE_NAME${NC}"
        else
             echo -e "   ${RED}❌ Failed: $INSTANCE_NAME${NC}"
        fi
    done
done

echo -e "\n\n${CYAN}========================================${NC}"
echo -e "${GREEN} ✅ EXPERIMENTS COMPLETED!${NC}"
echo -e "${CYAN}========================================${NC}"
echo "Results saved to: $OUTPUT_DIR"
