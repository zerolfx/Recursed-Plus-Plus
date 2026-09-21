@echo off
setlocal
cd /d "%~dp0"
if not exist runtime\Recursed.exe (
  echo Prepare the isolated game copy first: powershell -File tools\prepare-runtime.ps1
  pause
  exit /b 1
)
if not exist build\recursed_peek.dll (
  echo Build the plugin first: build.cmd
  pause
  exit /b 1
)
set RECURSED_PEEK_TEST_INPUT=
build\recursed_peek.exe
if errorlevel 1 pause
