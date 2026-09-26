# RM Handheld

Turn a Red Magic 10 Pro, a GameSir X5 Lite, an ESP32-S3 Super Mini, and a
240×320 ST7789 display into a compact handheld with a live hardware dashboard.

This repository contains two programs:

- `android/`: a Kotlin app that reads phone telemetry and sends it over BLE.
- `firmware/`: ESP-IDF firmware that receives telemetry, draws the dashboard,
  reads the directly wired X5 Lite controls, and exposes a BLE HID gamepad.

ESP-IDF/C++ is used for the ESP32 rather than MicroPython or Arduino because
this build needs GPIO/ADC input, a BLE HID peripheral, a BLE GATT server, and
LVGL running together. The companion APK is native Kotlin and does not require
root.

## Current milestone

Version **0.2.0 direct GPIO** replaces the old USB-host input path. The
ESP32-S3 now reads the controller buttons and analog controls directly, then
forwards them to Android as a BLE HID gamepad. The TFT remains 320×240
landscape with the crisp RGB byte-order fix.

The phone telemetry limits remain the same:

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

## Direct controller pin map

The firmware expects active-low digital inputs and the following analog
signals. Menu is the **TX-labelled GPIO43** pad; Home is **GPIO48**.

| Control | GPIO | Control | GPIO |
| --- | ---: | --- | ---: |
| A | 21 | B | 38 |
| X | 39 | Y | 40 |
| LB | 41 | RB | 42 |
| D-pad Up | 47 | D-pad Down | 44 / RX |
| D-pad Left | 2 | D-pad Right | 4 |
| L3 | 5 | R3 | 6 |
| View | 7 | Menu | 43 / TX |
| Home | 48 |  |  |

| Analog signal | GPIO |
| --- | ---: |
| Left stick X | 1 |
| Left stick Y | 14 |
| Right stick X | 15 |
| Right stick Y | 16 |
| Left trigger | 17 |
| Right trigger | 18 |

Keep analog inputs between 0 V and 3.3 V. Leave both sticks and triggers
untouched during the first second after RESET for calibration. Disconnect all
power before hardware work, and have an electronics-experienced adult handle
battery wiring and soldering.

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

1. Flash the merged web-flash BIN at address `0x0`, then press RESET.
2. Do not touch the sticks or triggers during the first second.
3. Pair `RM Handheld` in Android Bluetooth settings.
4. Open the APK and press **Connect** for phone telemetry.
5. Test A/B/X/Y, D-pad, bumpers, Menu, Home, sticks, and triggers in an Android
   game-controller tester before opening a game.
6. Hold **View + Menu** for two seconds to test dashboard navigation.

The dashboard appears without waiting for the APK. The USB serial terminal
remains available because this build no longer switches the ESP32-S3 into USB
host mode. A lit backlight alone does not prove that the LCD controller
received SPI data; startup colors followed by the dashboard do.

## Testing without optional hardware

The TFT and MAX17048 are not required for BLE controller input. Leave the TFT
unconnected if needed, flash the firmware, pair `RM Handheld`, and test the
direct controls first. The native USB connection can remain attached for serial
diagnostics.

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
