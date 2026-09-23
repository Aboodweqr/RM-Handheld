# RM Handheld

Turn a Red Magic 10 Pro, a GameSir X5 Lite, an ESP32-S3 Super Mini, and a
240×320 ST7789 display into a compact handheld with a live hardware dashboard.

This repository contains two programs:

- `android/`: a Kotlin app that reads phone telemetry and sends it over BLE.
- `firmware/`: ESP-IDF firmware that receives telemetry, draws the dashboard,
  reads a USB HID controller, and exposes a BLE HID gamepad.

ESP-IDF/C++ is used for the ESP32 rather than MicroPython or Arduino because
this build needs the native USB host stack, a BLE HID peripheral, a BLE GATT
server, and LVGL running together. The companion APK is native Kotlin and does
not require root.

## Current milestone

Version **0.1.5 source** fixes discovery and adds an LCD startup check. This is
source code, **not** a flashable firmware image or an installable APK. The old
0.1.4 standalone ZIP remains the only prebuilt image until the firmware and app
workflows compile this revision. Flashing that old image will not apply these
changes. See [RELEASE_NOTES_v0.1.5.md](RELEASE_NOTES_v0.1.5.md).

The first milestone is deliberately honest about what Android exposes:

| Reading | Expected on Red Magic 10 Pro | Fallback |
| --- | --- | --- |
| Phone battery %, voltage, temperature, charge state | Reliable | `--` |
| Current and estimated watts | Usually available | `--` |
| Android thermal status/headroom | Reliable on supported Android versions | status only |
| RAM usage | Reliable | `--` |
| Wi-Fi RSSI and link rate | Available after Nearby Wi-Fi permission | `--` |
| CPU/GPU clocks, usage, temperatures | Vendor/kernel dependent | `N/A` card |
| Controller battery | GameSir X5 Lite is wired and has no battery | `WIRED` |
| ESP battery | Hidden until a MAX17048 is added | `NO GAUGE` |

The generic HID report parser is included and host-tested. The USB diagnostic
prints the X5 Lite's VID/PID, report descriptor, and input reports so its exact
layout can be verified on real hardware instead of guessing.

## Dashboard controls

- Hold **View + Menu** for two seconds to enter or leave dashboard control.
- D-pad left/right or LB/RB changes page.
- A opens the selected card; B returns to the overview.
- Dashboard mode sends a neutral gamepad report to Android and consumes the
  navigation keys, so it cannot move a character in the game at the same time.
- It exits automatically after 20 seconds without input.

See [docs/UI.md](docs/UI.md), [docs/APK.md](docs/APK.md), and
[docs/ui-preview.svg](docs/ui-preview.svg).

## Display wiring

The pin defaults match the pictured ESP32-S3 Super Mini and the seven-pin
ST7789 module. Confirm the printed labels on the modules before powering them.

| TFT pin | ESP32-S3 pin |
| --- | --- |
| VCC | 3V3 |
| GND | GND |
| SCL | GPIO12 (SPI clock) |
| SDA | GPIO11 (SPI MOSI) |
| CS | GPIO10 |
| DC | GPIO9 |
| RST | GPIO8 |

The TFT is a 3.3 V device. `SDA` on this board means SPI data/MOSI, not I²C SDA.

## Build the Android APK

After this source is uploaded to a GitHub repository, its workflow creates an
installable debug APK on every push:

1. Open the repository's **Actions** tab.
2. Open **Android APK**, then **Run workflow**.
3. Download the `rm-handheld-debug-apk` artifact.
4. Install `app-debug.apk` on the phone and allow Bluetooth, notifications,
   and Nearby Wi-Fi when Android asks.

Android Studio can also open the `android/` directory. Use JDK 17 and Android
SDK 35, then run the `app` configuration. The APK runs normally on Android 16.

## Build and flash the ESP32-S3

The pinned firmware build uses ESP-IDF 5.5.5 (the automated workflow installs
that version). An ESP-IDF 5.4 environment cannot build this source:

```bash
cd firmware
idf.py set-target esp32s3
idf.py build
idf.py -p YOUR_PORT flash monitor
```

The **Firmware** GitHub Action also produces `rm-handheld-firmware-binaries` containing the
bootloader, partition table, app binary, and a merged flash image.

For the source package, build with ESP-IDF 5.5.5 or let that GitHub workflow
produce the merged image. Copying the source ZIP or running the old v0.1.4
`flash.bat` does not update the device to v0.1.5.

## First hardware run

1. Power the ESP32 from a regulated 5 V input or its USB-C port while testing.
2. Flash firmware and open the serial monitor at 115200 baud.
3. Install the APK, press **Connect**, and choose `RM Handheld`.
4. Confirm the six cards update.
5. Plug the X5 Lite into the ESP32's native USB host connection.
6. Save the lines beginning with `USB_DESCRIPTOR` and `USB_REPORT` from the
   serial monitor. They are enough to add a verified X5 Lite profile if its
   descriptor is not standard HID.

This revision shows green/magenta LCD test colors for about 0.8 seconds, then
draws the dashboard immediately without waiting for the APK. With this firmware,
`RM Handheld` should be discoverable in the phone's Bluetooth settings even if
the app is not installed. USB host starts 10 seconds later so the boot messages
can be observed on the Super Mini's native USB serial port; Windows may drop
that serial port when USB host takes over the shared internal PHY.

If the startup colors never appear, check the exact screen controller marking,
display pin labels, connections, and the serial log. A lit backlight alone does
not establish that the LCD controller received SPI data. No Windows display
driver is needed for a screen wired directly to an ESP32.

Before the phone connects, the dashboard's cards show `--`, `N/A`, or
`waiting`. Persistent random colours mean the display did not initialize;
they are not a Bluetooth waiting screen.

## What you can test before the TFT arrives

The display and MAX17048 are not required for the USB/BLE core. Leave the TFT
pins unconnected, flash the firmware, install the APK, and test pairing plus
controller input first. The app itself previews all phone readings even before
it is connected. If the Super Mini has only one native USB connector, it cannot
be connected to the PC and act as the controller's USB host through that same
connector at the same time; a separate 3.3 V UART monitor is needed to capture
logs during USB-host testing.

## Power boundary

Software cannot make an unknown raw USB-C or hub pad safe. Do not feed the 8 V
rail you measured into the ESP32, TFT, phone, or a bare LiPo cell. Use a proper
protected battery/charger/regulator board and verify its regulated output. The
MAX17048 hook is telemetry-only; it is not a charger or protection circuit.

## Layout

```text
android/       Kotlin app and foreground BLE telemetry service
firmware/      ESP-IDF firmware and hardware integration
protocol/      Shared 20-byte BLE packet definition
tests/         Portable C++ protocol/HID/navigation tests
docs/          UI, protocol, setup, and troubleshooting notes
```

Licensed under MIT. External dependencies keep their own licenses.
