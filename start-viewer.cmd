@echo off
setlocal
cd /d "%~dp0"

where node >nul 2>nul
if errorlevel 1 (
  echo Node.js was not found.
  echo Install Node.js 22 LTS or newer, then run this file again.
  pause
  exit /b 1
)

if not exist node_modules (
  echo Installing packages for the first run...
  call npm.cmd install
  if errorlevel 1 (
    echo npm install failed.
    pause
    exit /b 1
  )
)

echo Starting STRAKON SMD Viewer V1...
call npm.cmd run dev
pause
