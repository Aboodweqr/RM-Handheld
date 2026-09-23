# Android companion APK

The APK has two jobs: preview the phone readings and send them to the ESP32.
It is deliberately small, native Kotlin, offline, and does not require root.

## What appears in the app

The main screen mirrors the TFT overview in a two-column, three-row card grid:

| Card | Source | Behavior |
| --- | --- | --- |
| Phone battery | Android battery broadcast | percentage, charge state, and voltage |
| Charge power | `BatteryManager` current × battery voltage | estimate in watts, or `N/A` |
| Battery temperature | Android battery broadcast | normally available in 0.1 °C steps |
| Thermal | `PowerManager` | Android thermal state and remaining-headroom estimate |
| CPU / RAM | `/proc`, cpufreq probes, and `ActivityManager` | RAM is normal; CPU details may be `N/A` |
| GPU | Qualcomm KGSL and thermal-zone probes | shown only when the Red Magic kernel permits reads |

The watt figure is a battery-side estimate from the phone's battery voltage and
net battery current. It is not a USB-PD measurement of power at the wall
charger. Likewise, `charger temperature` means a phone-side charger/USB thermal
zone if the kernel exposes one; it cannot read the temperature inside the
external charger brick.

Below the cards, the app shows Wi-Fi link details, the number of BLE GATT
links Android reports, `WIRED` for the X5 Lite, and `NO GAUGE` for the ESP32
battery until the optional MAX17048 is installed.

## How data reaches the screen

1. Pressing **Connect** starts a foreground service with a visible, low-priority
   notification.
2. The service scans for the ESP32 device named `RM Handheld` and reconnects if
   the link drops.
3. Once per second, the collector reads public Android APIs and tries a small
   set of read-only vendor/kernel sensor paths.
4. It sends four CRC-protected 20-byte BLE packets: core battery data,
   performance data, network data, and a display-safe Wi-Fi name.
5. The ESP32 rejects malformed packets, stores 60 samples, and redraws the TFT
   twice per second.

The app has no `INTERNET` permission and contains no account, advertising,
analytics, or cloud code.

## Permissions

- Bluetooth scan/connect: required to find and connect to the ESP32.
- Notifications: used by the foreground service while telemetry is active.
- Nearby Wi-Fi: optional; without it, the Network page shows `N/A`.
- Location on Android 11 or older: the legacy permission Android requires for
  BLE discovery on those versions.

If optional Wi-Fi or notification permission is denied, Bluetooth telemetry
can still start as long as the Bluetooth permissions were granted.

## Honest Android limitations

Battery percentage, battery temperature, charge state, RAM use, and Android's
thermal severity are expected to work. Exact CPU/GPU utilization, clock,
temperature, charger temperature, and battery cycle count depend on which
files the Red Magic 10 Pro's Android 16 build exposes to ordinary apps. A
failed read is caught and displayed as `N/A`; the app never substitutes a fake
zero.

This is also why the app does not promise a game FPS value or a complete list
of every Bluetooth audio/input profile. It can report the handheld link and
the public BLE GATT connection count.

## Building the APK

GitHub Actions runs the unit test and creates a debug APK on every Android
source change. In the repository, open **Actions → Android APK → Run workflow**,
then download the `rm-handheld-debug-apk` artifact. The file inside is
`app-debug.apk`.

For a local build, open the `android/` directory in Android Studio with JDK 17
and Android SDK 35. Build the `app` debug configuration. The output is:

```text
android/app/build/outputs/apk/debug/app-debug.apk
```

Adding a MAX17048 later does not require an APK change. Only the ESP32
`fuel_gauge.cpp` driver and its I²C pin configuration need to be completed.
