@echo OFF
TITLE ALNS Development Environment

echo ======================================================================
echo Initializing Visual Studio x64 and Conda 'alns-env' Environment...
echo ======================================================================
echo.

REM --- STEP 1: Initialize Visual Studio Environment ---
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
if %errorlevel% neq 0 (
    echo [ERROR] Failed to initialize Visual Studio environment.
    echo Please check the path to vcvarsall.bat.
    pause
    exit /b %errorlevel%
)
echo Visual Studio x64 environment is ready.
echo.

REM --- STEP 2: Activate Conda Environment using activate.bat ---
call C:\Users\ASUS\miniconda3\Scripts\activate.bat alns-env
if %errorlevel% neq 0 (
    echo [ERROR] Failed to activate Conda environment 'alns-env'.
    pause
    exit /b %errorlevel%
)
echo Conda environment 'alns-env' is active.
echo.

echo ======================================================================
echo Your hybrid development environment is ready!
echo ======================================================================
echo.

REM Start a new command prompt session in the current project directory
cmd.exe /K "cd /d %~dp0"