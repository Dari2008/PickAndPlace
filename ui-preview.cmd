@echo off
rem Display preview on Windows: double-click. Runs the real Pi display code in simulation (WSL) and opens it in the
rem browser. Close this window (or Ctrl+C) to stop. Details: tools\ui-preview.cjs
title Pick ^& Place display preview
where node >nul 2>nul || (echo Node.js is missing: https://nodejs.org & pause & exit /b 1)
where wsl >nul 2>nul || (echo WSL is missing: wsl --install -d Ubuntu & pause & exit /b 1)
node "%~dp0tools\ui-preview.cjs" %*
if errorlevel 1 pause
