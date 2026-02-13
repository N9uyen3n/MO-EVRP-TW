# =============================================================================
# Script: run_fairness_experiments.ps1
# Mục đích: Chạy tự động 3 baselines cho thực nghiệm Driver Equity
# =============================================================================

param(
    [string]$BuildDir = "build/Release",
    [string]$OutputDir = "results/fairness_experiments",
    [string]$DataDir = "data/test1"
)

# Màu sắc cho output
function Write-ColorOutput {
    param([string]$Message, [string]$Color = "White")
    Write-Host $Message -ForegroundColor $Color
}

Write-ColorOutput "========================================" "Cyan"
Write-ColorOutput " FAIRNESS EXPERIMENT RUNNER" "Cyan"
Write-ColorOutput "========================================" "Cyan"

# Kiểm tra executable
$exe = Join-Path $BuildDir "test16.exe"
if (-not (Test-Path $exe)) {
    Write-ColorOutput "❌ ERROR: Không tìm thấy $exe" "Red"
    Write-ColorOutput "Vui lòng build project trước: cmake --build build --config Release" "Yellow"
    exit 1
}

# Tạo thư mục output
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

# Lấy danh sách instances
$instances = Get-ChildItem -Path $DataDir -Filter "*.txt" | Select-Object -First 10

Write-ColorOutput "`n📁 Tìm thấy $($instances.Count) instances trong $DataDir" "Green"
$instances | ForEach-Object { Write-Host "   - $($_.Name)" }

# Định nghĩa 3 scenarios
$scenarios = @(
    @{
        Name = "Baseline_A_CostOnly"
        Mode = "COST_ONLY"
        Description = "Cost-Only (w_F = 0)"
        Color = "Yellow"
    },
    @{
        Name = "Baseline_B_StaticFairness"
        Mode = "STATIC_FAIRNESS"
        Description = "Static Fairness (w_F = 1.0)"
        Color = "Magenta"
    },
    @{
        Name = "Proposed_AdaptiveFairness"
        Mode = "ADAPTIVE_FAIRNESS"
        Description = "Adaptive Fairness (w_F: 0.1 → 5.0)"
        Color = "Cyan"
    }
)

# Chạy thực nghiệm
$totalRuns = $scenarios.Count * $instances.Count
$currentRun = 0

foreach ($scenario in $scenarios) {
    Write-ColorOutput "`n`n╔═══════════════════════════════════════════════════════════════╗" $scenario.Color
    Write-ColorOutput "║  SCENARIO: $($scenario.Description)" $scenario.Color
    Write-ColorOutput "╚═══════════════════════════════════════════════════════════════╝" $scenario.Color
    
    $scenarioDir = Join-Path $OutputDir $scenario.Name
    New-Item -ItemType Directory -Force -Path $scenarioDir | Out-Null
    
    foreach ($instance in $instances) {
        $currentRun++
        $instanceName = $instance.BaseName
        $instancePath = $instance.FullName
        
        Write-ColorOutput "`n[$currentRun/$totalRuns] Running: $instanceName with $($scenario.Mode)" "White"
        
        $outputSubDir = Join-Path $scenarioDir $instanceName
        
        # Chạy solver (CHÚ Ý: Cần update command này theo cách gọi thực tế của bạn)
        # Giả sử test16.exe nhận --instance, --mode, --output
        $args = @(
            "--instance", "`"$instancePath`"",
            "--mode", $scenario.Mode,
            "--output", "`"$outputSubDir`""
        )
        
        try {
            & $exe $args
            Write-ColorOutput "   ✅ Completed: $instanceName" "Green"
        } catch {
            Write-ColorOutput "   ❌ Failed: $instanceName - $($_.Exception.Message)" "Red"
        }
    }
}

Write-ColorOutput "`n`n========================================" "Cyan"
Write-ColorOutput " ✅ EXPERIMENTS COMPLETED!" "Green"
Write-ColorOutput "========================================" "Cyan"
Write-ColorOutput "Results saved to: $OutputDir" "White"
Write-ColorOutput "`nNext steps:" "Yellow"
Write-ColorOutput "  1. Analyze logs in each scenario folder" "White"
Write-ColorOutput "  2. Run: jupyter notebook ALNS_Log_Analysis.ipynb" "White"
Write-ColorOutput "  3. Compare Pareto fronts and convergence" "White"
