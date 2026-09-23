RM HANDHELD v0.1.5 - FIXED WINDOWS FLASHER
================================================

This package fixes the broken/truncated esptool.exe in the earlier ZIP.
It contains the real compiled v0.1.5 ESP32-S3 firmware.

WHAT YOU NEED
- Windows 10 or 11
- An internet connection on the first run
- The ESP32-S3 connected by a data-capable USB cable

HOW TO FLASH
1. Extract this whole ZIP to a normal folder.
2. Disconnect the GameSir controller for the first test.
3. Close Arduino Serial Monitor and any program using the COM port.
4. Double-click flash.bat.
5. Enter the ESP32-S3 port, for example COM5.
6. Wait for a real SUCCESS message, then press RESET on the ESP32-S3.

The first run downloads Espressif's official Windows esptool v5.1.0 package
(about 60 MB). The package is checked against Espressif's published SHA-256
before Windows is allowed to run it. Python, WSL, and ESP-IDF are not needed.

IF CONNECTING FAILS
- Hold BOOT.
- Tap and release RESET while still holding BOOT.
- Release BOOT.
- Run flash.bat again.

EXPECTED SCREEN
- Green and magenta test colors for about one second.
- Then the RM Handheld dashboard.

If the TFT backlight is on but no green/magenta test appears after a confirmed
successful flash, the next check is the TFT wiring/pin mapping.
