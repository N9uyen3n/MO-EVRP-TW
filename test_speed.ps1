param(
    [string]$InstanceFile = "data\solomon\r105C15.txt"
)

$ErrorActionPreference = "Stop"

Write-Host "=========================================================" -ForegroundColor Cyan
Write-Host "   LOCAL SEARCH SPEED COMPARISON TOOL" -ForegroundColor Cyan
Write-Host "=========================================================" -ForegroundColor Cyan

$srcFile = "src\alns\LocalSearch.cpp"
$testFile = "test_localsearch\LocalSearch.cpp"
$bakFile = "src\alns\LocalSearch.cpp.bak"

if (-not (Test-Path $testFile)) {
    Write-Host "[Error] Cannot find $testFile" -ForegroundColor Red
    exit
}

# 1. Backup original
Write-Host "Backing up $srcFile..."
Copy-Item -Path $srcFile -Destination $bakFile -Force

try {
    # 2. Run baseline
    Write-Host "`n[1/2] COMPILING AND RUNNING BASELINE (src\alns\LocalSearch.cpp)..." -ForegroundColor Yellow
    cmake --build .\build --config Release --target TestALNS | Out-Null
    
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $outOriginal = .\build\Release\TestALNS.exe $InstanceFile
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
    Write-Host "`n[2/2] COMPILING AND RUNNING TEST VERSION (test_localsearch\LocalSearch.cpp)..." -ForegroundColor Yellow
    Copy-Item -Path $testFile -Destination $srcFile -Force
    cmake --build .\build --config Release --target TestALNS | Out-Null
    
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $outTest = .\build\Release\TestALNS.exe $InstanceFile
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
    Write-Host "`nRestoring original $srcFile..."
    Copy-Item -Path $bakFile -Destination $srcFile -Force
    Remove-Item -Path $bakFile -Force
    Write-Host "Done!" -ForegroundColor Green
}
