# RM Handheld 0.1.5 source repair

**Source only.** This archive contains no new firmware BIN and no new APK. The
0.1.4 prebuilt image cannot contain the changes listed below.

- ST7789: draw a full-screen green/magenta test for ~0.8 s at boot, then build
  the 320x240 LVGL dashboard, force its first refresh, and log startup failures
  by stage. The dashboard shows before any phone connection.
- BLE: advertise `RM Handheld` and the standard HID service in the primary
  advertisement; include the private telemetry UUID in the scan response.
  Record successful advertising and errors in the USB serial boot log.
- USB: defer starting host for ten seconds and log installation failures
  without restarting the display and BLE stack. The S3's USB host and built-in
  USB serial port share an internal PHY.
- Android: scan for a live advertisement rather than endlessly reconnecting to
  a saved pairing left by an older flash. Set app version to 0.1.5.
- Console: select native USB Serial/JTAG for the first ten seconds of logs.

## What to check after building and flashing this revision

1. Disconnect the controller during the first test. Reset the ESP32 and watch
   for green and magenta, then the dashboard. It works without an APK.
2. In the first ten seconds open a 115200-baud serial monitor and save the
   `rmh_display` and `rmh_ble` lines. Expect `Advertising as RM Handheld`.
3. Scan Android Bluetooth settings for `RM Handheld`. If the name appears but
   the app fails to connect, forget any saved `RM Handheld` pairing and install
   the newly built 0.1.5 APK.
4. If the test colors never appear but the display logs say they were sent,
   the software cannot confirm the LCD actually received SPI data: compare
   printed pin labels to `firmware/main/board_config.hpp` and confirm the
   display's controller is ST7789V. A backlight alone is inconclusive.

The actual screen and BLE radio need a physical test; host C++ tests cover the
telemetry, HID parser, and navigation logic only.
