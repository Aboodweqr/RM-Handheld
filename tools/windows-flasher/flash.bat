@echo off
setlocal EnableExtensions DisableDelayedExpansion
title RM Handheld v0.1.5 - Fixed Windows Flasher

cd /d "%~dp0"
set "RMH_BIN=%CD%\prebuilt\rm_handheld_v0.1.5_merged.bin"
set "RMH_SCRIPT=%CD%\flash.ps1"

if not exist "%RMH_BIN%" (
    echo ERROR: Missing prebuilt\rm_handheld_v0.1.5_merged.bin
    echo Extract the complete ZIP before running flash.bat.
    goto :failed
)

if not exist "%RMH_SCRIPT%" (
    echo ERROR: Missing flash.ps1
    echo Extract the complete ZIP before running flash.bat.
    goto :failed
)

echo.
echo RM Handheld v0.1.5 fixed Windows flasher
echo No Python, WSL, or ESP-IDF installation is required.
echo On its first run it downloads the official Espressif flashing tool.
echo Close Arduino Serial Monitor before flashing.
echo.
echo Connected ESP32 ports usually look like COM3, COM4, or COM5.
set /p "RMH_PORT=Enter the ESP32-S3 COM port: "
if "%RMH_PORT%"=="" (
    echo ERROR: No COM port entered.
    goto :failed
)

echo.
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%RMH_SCRIPT%" -Port "%RMH_PORT%" -Firmware "%RMH_BIN%"
set "RMH_RESULT=%ERRORLEVEL%"

if not "%RMH_RESULT%"=="0" goto :failed

echo.
echo SUCCESS: RM Handheld v0.1.5 was really flashed and verified.
echo Disconnect the GameSir controller for this first test, then press RESET.
echo The TFT should show green and magenta briefly, then the dashboard.
pause
exit /b 0

:failed
echo.
echo FAILED: Nothing was reported as successful. Read the error above.
echo If connection fails, hold BOOT, tap RESET, release BOOT, then try again.
pause
exit /b 1
