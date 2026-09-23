package com.aboodweqr.rmhandheld

import android.app.ActivityManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.BluetoothManager
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.net.wifi.WifiInfo
import android.net.wifi.WifiManager
import android.os.BatteryManager
import android.os.Build
import android.os.PowerManager
import java.io.File
import kotlin.math.abs
import kotlin.math.roundToInt

data class TelemetrySnapshot(
    val batteryPercent: Int? = null,
    val charging: Boolean = false,
    val full: Boolean = false,
    val batteryTempDeciC: Int? = null,
    val voltageMv: Int? = null,
    val currentMa: Int? = null,
    val powerCentiW: Int? = null,
    val cycleCount: Int? = null,
    val thermalStatus: Int? = null,
    val thermalHeadroomPercent: Int? = null,
    val ramUsagePercent: Int? = null,
    val cpuUsagePercent: Int? = null,
    val gpuUsagePercent: Int? = null,
    val cpuMhz: Int? = null,
    val gpuMhz: Int? = null,
    val cpuTempDeciC: Int? = null,
    val gpuTempDeciC: Int? = null,
    val chargerTempDeciC: Int? = null,
    val wifiConnected: Boolean = false,
    val ssid: String? = null,
    val wifiRssiDbm: Int? = null,
    val rxLinkMbps: Int? = null,
    val txLinkMbps: Int? = null,
    val bluetoothGattConnections: Int? = null,
)

class TelemetryCollector(private val context: Context) {
    private var previousCpuTotal: Long? = null
    private var previousCpuIdle: Long? = null
    private var lastHeadroomTimeMs = 0L
    private var cachedHeadroomPercent: Int? = null

    fun collect(): TelemetrySnapshot {
        val battery = context.registerReceiver(null, IntentFilter(Intent.ACTION_BATTERY_CHANGED))
        val batteryManager = context.getSystemService(BatteryManager::class.java)
        val level = battery?.intExtra(BatteryManager.EXTRA_LEVEL)?.takeIf { it >= 0 }
        val scale = battery?.intExtra(BatteryManager.EXTRA_SCALE)?.takeIf { it > 0 }
        val percent = if (level != null && scale != null) level * 100 / scale else null
        val status = battery?.intExtra(BatteryManager.EXTRA_STATUS)
        val charging = status == BatteryManager.BATTERY_STATUS_CHARGING
        val full = status == BatteryManager.BATTERY_STATUS_FULL
        val voltage = battery?.intExtra(BatteryManager.EXTRA_VOLTAGE)?.takeIf { it > 0 }
        val temperature = battery?.intExtra(BatteryManager.EXTRA_TEMPERATURE)
            ?.takeIf { it in -500..1500 }
        val currentMicroA = batteryManager
            .getLongProperty(BatteryManager.BATTERY_PROPERTY_CURRENT_NOW)
            .takeIf { it != Long.MIN_VALUE && abs(it) < 20_000_000L }
        val currentMa = currentMicroA?.div(1000L)?.toInt()
        val powerCentiW = if (voltage != null && currentMa != null) {
            (abs(voltage.toLong() * currentMa.toLong()) / 10_000L).toInt()
        } else null

        val cycleCount = if (Build.VERSION.SDK_INT >= 34) {
            battery?.getIntExtra(BatteryManager.EXTRA_CYCLE_COUNT, -1)?.takeIf { it >= 0 }
        } else null

        val powerManager = context.getSystemService(PowerManager::class.java)
        val thermalStatus = powerManager.currentThermalStatus
        val now = android.os.SystemClock.elapsedRealtime()
        if (Build.VERSION.SDK_INT >= 30 && now - lastHeadroomTimeMs >= 10_000L) {
            val headroom = powerManager.getThermalHeadroom(0)
            if (!headroom.isNaN()) {
                cachedHeadroomPercent = ((1f - headroom) * 100f).roundToInt().coerceIn(0, 100)
            }
            lastHeadroomTimeMs = now
        }

        val memory = ActivityManager.MemoryInfo().also {
            context.getSystemService(ActivityManager::class.java).getMemoryInfo(it)
        }
        val ram = if (memory.totalMem > 0) {
            (((memory.totalMem - memory.availMem) * 100L) / memory.totalMem).toInt()
        } else null

        val wifi = readWifi()
        return TelemetrySnapshot(
            batteryPercent = percent,
            charging = charging,
            full = full,
            batteryTempDeciC = temperature,
            voltageMv = voltage,
            currentMa = currentMa,
            powerCentiW = powerCentiW,
            cycleCount = cycleCount,
            thermalStatus = thermalStatus,
            thermalHeadroomPercent = cachedHeadroomPercent,
            ramUsagePercent = ram,
            cpuUsagePercent = readCpuUsage(),
            gpuUsagePercent = readGpuUsage(),
            cpuMhz = readCpuMhz(),
            gpuMhz = readLong("/sys/class/kgsl/kgsl-3d0/devfreq/cur_freq")
                ?.div(1_000_000L)?.toInt(),
            cpuTempDeciC = readThermalTemperature("cpu"),
            gpuTempDeciC = readThermalTemperature("gpu"),
            chargerTempDeciC = readThermalTemperature("charger")
                ?: readThermalTemperature("usb"),
            wifiConnected = wifi != null,
            ssid = wifi?.ssid?.removeSurrounding("\"")?.takeUnless { it == "<unknown ssid>" },
            wifiRssiDbm = wifi?.rssi,
            rxLinkMbps = wifi?.rxLinkSpeedMbps?.takeIf { it >= 0 },
            txLinkMbps = wifi?.txLinkSpeedMbps?.takeIf { it >= 0 },
            bluetoothGattConnections = runCatching {
                context.getSystemService(BluetoothManager::class.java)
                    .getConnectedDevices(BluetoothProfile.GATT).size
            }.getOrNull(),
        )
    }

    private fun Intent.intExtra(key: String): Int? =
        getIntExtra(key, Int.MIN_VALUE).takeIf { it != Int.MIN_VALUE }

    @Suppress("DEPRECATION")
    private fun readWifi(): WifiInfo? = runCatching {
        val connectivity = context.getSystemService(ConnectivityManager::class.java)
        val capabilities = connectivity.getNetworkCapabilities(connectivity.activeNetwork)
        if (capabilities?.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) != true) return null
        if (Build.VERSION.SDK_INT >= 31) {
            capabilities.transportInfo as? WifiInfo
        } else {
            context.applicationContext.getSystemService(WifiManager::class.java).connectionInfo
        }
    }.getOrNull()

    private fun readCpuUsage(): Int? = runCatching {
        val parts = File("/proc/stat").useLines { lines ->
            lines.first().trim().split(Regex("\\s+")).drop(1).map(String::toLong)
        }
        if (parts.size < 4) return null
        val idle = parts[3] + parts.getOrElse(4) { 0L }
        val total = parts.sum()
        val oldTotal = previousCpuTotal
        val oldIdle = previousCpuIdle
        previousCpuTotal = total
        previousCpuIdle = idle
        if (oldTotal == null || oldIdle == null || total <= oldTotal) return null
        val totalDelta = total - oldTotal
        val idleDelta = idle - oldIdle
        (((totalDelta - idleDelta) * 100L) / totalDelta).toInt().coerceIn(0, 100)
    }.getOrNull()

    private fun readGpuUsage(): Int? = runCatching {
        val values = File("/sys/class/kgsl/kgsl-3d0/gpubusy").readText()
            .trim().split(Regex("\\s+")).map(String::toLong)
        if (values.size < 2 || values[1] <= 0L) return null
        ((values[0] * 100L) / values[1]).toInt().coerceIn(0, 100)
    }.getOrNull()

    private fun readCpuMhz(): Int? {
        val values = (0..15).mapNotNull { cpu ->
            readLong("/sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_cur_freq")
        }
        return values.maxOrNull()?.div(1000L)?.toInt()
    }

    private fun readThermalTemperature(kind: String): Int? = runCatching {
        File("/sys/class/thermal").listFiles()
            ?.asSequence()
            ?.filter { it.name.startsWith("thermal_zone") }
            ?.firstNotNullOfOrNull { zone ->
                val type = runCatching { File(zone, "type").readText().trim().lowercase() }
                    .getOrNull() ?: return@firstNotNullOfOrNull null
                if (!type.contains(kind)) return@firstNotNullOfOrNull null
                val raw = readLong(File(zone, "temp").absolutePath)
                    ?: return@firstNotNullOfOrNull null
                when {
                    raw > 1000 -> (raw / 100L).toInt() // millidegrees to tenths.
                    raw in -500..1500 -> raw.toInt()
                    else -> null
                }
            }
    }.getOrNull()

    private fun readLong(path: String): Long? =
        runCatching { File(path).readText().trim().toLong() }.getOrNull()
}
