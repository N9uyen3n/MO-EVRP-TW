param(
    [string]$InstanceFile = "data/solomon/c101_21.txt"
)

$InstanceFile = $InstanceFile.Replace('\', '/')
$ErrorActionPreference = "Stop"

Write-Host "=========================================================" -ForegroundColor Cyan
Write-Host "   ROUTE EVALUATION SPEED COMPARISON TOOL (WSL RELEASE)" -ForegroundColor Cyan
Write-Host "=========================================================" -ForegroundColor Cyan

$srcCpp = "src/core/Route.cpp"
$srcH = "include/core/Route.h"
$testCpp = "test_localsearch/Route.cpp"
$testH = "test_localsearch/Route.h"
$bakCpp = "src/core/Route.cpp.bak"
$bakH = "include/core/Route.h.bak"

if (-not (Test-Path $testCpp) -or -not (Test-Path $testH)) {
    Write-Host "[Error] Cannot find $testCpp or $testH" -ForegroundColor Red
    exit
}

# 1. Backup original
Write-Host "Backing up original Route files..."
Copy-Item -Path $srcCpp -Destination $bakCpp -Force
Copy-Item -Path $srcH -Destination $bakH -Force

try {
    # 2. Run baseline
    Write-Host "`n[1/2] COMPILING AND RUNNING BASELINE (Original Route files)..." -ForegroundColor Yellow
    wsl cmake --build cmake-build-release-wsl --target TestALNS | Out-Null
    
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $outOriginal = wsl ./cmake-build-release-wsl/TestALNS $InstanceFile
    $sw.Stop()
    
    $matchLS = $outOriginal | Select-String -Pattern 'LocalSearch\s+([\d\.]+ ms)'
    if (-not $matchLS) {
        Write-Host "[Error] TestALNS failed or did not output expected format." -ForegroundColor Red
        Write-Host "Output:"
        $outOriginal | Write-Host
        exit
    }
    
    $lsTimeOrg = $matchLS.Matches.Groups[1].Value
    $totalTimeOrg = ($outOriginal | Select-String -Pattern 'Execution Time:\s+(\d+\s+ms)').Matches.Groups[1].Value
    $hvOrg = ($outOriginal | Select-String -Pattern 'HV\s+([\d\.]+)').Matches.Groups[1].Value

    Write-Host "-> LocalSearch Time: $lsTimeOrg" -ForegroundColor Green
    Write-Host "-> Total Time:       $totalTimeOrg" -ForegroundColor Green
    Write-Host "-> HV Score:         $hvOrg" -ForegroundColor Green

    # 3. Copy test version and run
    Write-Host "`n[2/2] COMPILING AND RUNNING TEST VERSION (test_localsearch\Route files)..." -ForegroundColor Yellow
    Copy-Item -Path $testCpp -Destination $srcCpp -Force
    Copy-Item -Path $testH -Destination $srcH -Force
    wsl cmake --build cmake-build-release-wsl --target TestALNS | Out-Null
    
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $outTest = wsl ./cmake-build-release-wsl/TestALNS $InstanceFile
    $matchLSTest = $outTest | Select-String -Pattern 'LocalSearch\s+([\d\.]+ ms)'
    if (-not $matchLSTest) {
        Write-Host "[Error] TestALNS failed or did not output expected format." -ForegroundColor Red
        Write-Host "Output:"
        $outTest | Write-Host
        exit
    }
    
    $lsTimeTest = $matchLSTest.Matches.Groups[1].Value
    $totalTimeTest = ($outTest | Select-String -Pattern 'Execution Time:\s+(\d+\s+ms)').Matches.Groups[1].Value
    $hvTest = ($outTest | Select-String -Pattern 'HV\s+([\d\.]+)').Matches.Groups[1].Value

    Write-Host "-> LocalSearch Time: $lsTimeTest" -ForegroundColor Green
    Write-Host "-> Total Time:       $totalTimeTest" -ForegroundColor Green
    Write-Host "-> HV Score:         $hvTest" -ForegroundColor Green

    # 4. Compare
    Write-Host "`n=========================================================" -ForegroundColor Cyan
    Write-Host "                       CONCLUSION                        " -ForegroundColor Cyan
    Write-Host "=========================================================" -ForegroundColor Cyan
    
    $orgMs = [double]($lsTimeOrg -replace ' ms','')
    $testMs = [double]($lsTimeTest -replace ' ms','')
    $diff = $orgMs - $testMs
    
    if ($diff -gt 0) {
        $percent = [math]::Round(($diff / $orgMs) * 100, 2)
        Write-Host "Test version is FASTER than baseline: " -NoNewline
        Write-Host "$diff ms ($percent`%)" -ForegroundColor Green
    } elseif ($diff -lt 0) {
        $diffAbs = -$diff
        $percent = [math]::Round(($diffAbs / $orgMs) * 100, 2)
        Write-Host "Test version is SLOWER than baseline: " -NoNewline
        Write-Host "$diffAbs ms ($percent`%)" -ForegroundColor Red
    } else {
        Write-Host "Speed is EXACTLY THE SAME." -ForegroundColor Yellow
    }
    
} finally {
    # 5. Restore
    Write-Host "`nRestoring original Route files..."
    Copy-Item -Path $bakCpp -Destination $srcCpp -Force
    Copy-Item -Path $bakH -Destination $srcH -Force
    Remove-Item -Path $bakCpp -Force
    Remove-Item -Path $bakH -Force
    Write-Host "Done!" -ForegroundColor Green
}
