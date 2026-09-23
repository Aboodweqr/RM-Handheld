# Dashboard and APK design

## TFT overview

The ST7789 is physically 240×320 and is rotated to a 320×240 landscape canvas.
The top 32 pixels are always a status bar; the remaining area holds six cards.

| Card | Large value | Small value |
| --- | --- | --- |
| Phone battery | `96%` | charging/full state |
| Charge power | `7.42 W` | charger temperature when exposed; otherwise current estimate |
| Battery temp | `38.4 C` | cycle count when Android exposes it |
| Thermal | `NOMINAL` | thermal headroom |
| CPU / RAM | CPU usage or `N/A` | max live clock and RAM use |
| GPU | GPU usage or `N/A` | live clock or kernel-blocked message |

The selected card has a mint two-pixel outline. Every number is received from
the APK except USB/BLE/controller-link information, which comes from the ESP32.

## Detail pages

| Page | Main content | Graph |
| --- | --- | --- |
| Battery + power | %, volts, current, watts, temperature, cycles | battery % and normalized watts |
| Temperatures | battery/charger temperature and Android thermal state | battery and charger temperatures |
| Performance | CPU, GPU, RAM, clocks, temperatures | CPU and GPU usage |
| Network | Wi-Fi name/RSSI, RX/TX link rates, BLE GATT count | Wi-Fi signal history |
| Controller | USB VID/PID, report counter, BLE state | no graph |
| Ambient | simple animated status ring | no graph |

The graph contains 60 samples. With the default one-second sample interval it
shows the last minute. Values that the Android kernel blocks are labeled `N/A`;
zero is never used as a fake sensor reading.

## Navigation

The screen is not touch-enabled. “Swipe” is a 190 ms slide animation triggered
by the controller:

1. Hold View + Menu for two seconds to enter dashboard mode.
2. The ESP32 immediately sends a neutral gamepad report so the chord is not
   forwarded into the game.
3. Use the D-pad to select a card, A to open it, and B to go back.
4. LB/RB moves between complete pages.
5. Hold View + Menu again, or leave it idle for 20 seconds, to return control to
   the game.

## What the APK does

The APK is a telemetry bridge, not a root tool and not an overlay:

1. `MainActivity` previews the values and says which optional sensors are
   unavailable on this phone.
2. The user presses **Connect** once.
3. `TelemetryService` scans for `RM Handheld`, connects to its private BLE GATT
   service, and keeps a low-priority notification visible while active.
4. `TelemetryCollector` takes a snapshot once per second.
5. The snapshot is encoded into four 20-byte packets: core, performance,
   network, and display-safe Wi-Fi identity. Each packet has a CRC so damaged
   data is rejected by the ESP32.
6. If BLE drops, the service scans and reconnects automatically.

No internet connection, account, analytics, or cloud storage is used.

## Android limitations

Battery data, RAM use, and Android's thermal severity API are normal public
APIs. Exact per-game FPS is not available to an ordinary app. CPU/GPU sysfs
files vary by Android build; Red Magic may expose some of them, but Android 16
can deny access. The APK probes read-only paths and catches permission errors.
The dashboard substitutes Android thermal status and RAM data when those vendor
sensors are unavailable.

Android also does not provide one simple public list of every currently
connected Bluetooth profile. The first build reports GATT connections and
shows the handheld's own BLE link explicitly. A later build can add profile-by-
profile counts if that is useful on the phone.

## Controller and battery labels

- GameSir X5 Lite: `WIRED`; it has no internal battery to measure.
- Handheld LiPo: `NO GAUGE` until a MAX17048 is fitted.
- Adding MAX17048 later changes only `fuel_gauge.cpp` and the controller page.
  The phone telemetry and APK do not need to change.
