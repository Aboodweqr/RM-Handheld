package com.aboodweqr.rmhandheld

import android.Manifest
import android.app.Activity
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.view.Gravity
import android.view.View
import android.widget.Button
import android.widget.GridLayout
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView

class MainActivity : Activity() {
    private data class CardViews(val value: TextView, val detail: TextView)

    private val handler = Handler(Looper.getMainLooper())
    private val cards = mutableListOf<CardViews>()
    private lateinit var statusText: TextView
    private lateinit var capabilityText: TextView
    private var service: TelemetryService? = null
    private var bound = false
    private val previewCollector by lazy { TelemetryCollector(applicationContext) }

    private val serviceConnection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName, binder: IBinder) {
            service = (binder as TelemetryService.LocalBinder).service
            bound = true
        }

        override fun onServiceDisconnected(name: ComponentName) {
            service = null
            bound = false
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(buildContent())
        handler.post(refreshUi)
    }

    override fun onStart() {
        super.onStart()
        bindService(Intent(this, TelemetryService::class.java), serviceConnection, Context.BIND_AUTO_CREATE)
    }

    override fun onStop() {
        if (bound) unbindService(serviceConnection)
        bound = false
        service = null
        super.onStop()
    }

    override fun onDestroy() {
        handler.removeCallbacksAndMessages(null)
        super.onDestroy()
    }

    private fun buildContent(): View {
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(18), dp(20), dp(18), dp(24))
            setBackgroundColor(Color.rgb(7, 11, 15))
        }
        root.addView(text("RM HANDHELD", 26f, Color.rgb(82, 224, 180), true))
        root.addView(text("Phone telemetry companion", 14f, Color.rgb(142, 158, 171), false).apply {
            setPadding(0, dp(2), 0, dp(14))
        })

        statusText = text("Stopped", 14f, Color.WHITE, true).apply {
            background = rounded(Color.rgb(18, 27, 34), Color.rgb(53, 74, 85))
            setPadding(dp(12), dp(10), dp(12), dp(10))
        }
        root.addView(statusText, LinearLayout.LayoutParams(-1, -2).apply {
            bottomMargin = dp(12)
        })

        val grid = GridLayout(this).apply {
            columnCount = 2
            rowCount = 3
            alignmentMode = GridLayout.ALIGN_BOUNDS
            useDefaultMargins = false
        }
        listOf(
            "PHONE BATTERY", "CHARGE POWER", "BATTERY TEMP",
            "THERMAL", "CPU / RAM", "GPU",
        ).forEachIndexed { index, title ->
            val card = createCard(title)
            cards += card.second
            val params = GridLayout.LayoutParams(
                GridLayout.spec(index / 2, 1f),
                GridLayout.spec(index % 2, 1f),
            ).apply {
                width = 0
                height = dp(112)
                setMargins(dp(4), dp(4), dp(4), dp(4))
            }
            grid.addView(card.first, params)
        }
        root.addView(grid, LinearLayout.LayoutParams(-1, -2))

        val buttonRow = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            setPadding(0, dp(14), 0, dp(8))
        }
        val start = button("CONNECT", Color.rgb(82, 224, 180), Color.rgb(3, 22, 18)) {
            requestPermissionsAndStart()
        }
        val stop = button("STOP", Color.rgb(31, 42, 50), Color.WHITE) {
            startService(Intent(this, TelemetryService::class.java).setAction(TelemetryService.ACTION_STOP))
        }
        buttonRow.addView(start, LinearLayout.LayoutParams(0, dp(50), 1f).apply { marginEnd = dp(6) })
        buttonRow.addView(stop, LinearLayout.LayoutParams(0, dp(50), 1f).apply { marginStart = dp(6) })
        root.addView(buttonRow)

        capabilityText = text("", 13f, Color.rgb(171, 183, 191), false).apply {
            setLineSpacing(0f, 1.25f)
            setPadding(dp(4), dp(8), dp(4), 0)
        }
        root.addView(capabilityText)

        return ScrollView(this).apply { addView(root) }
    }

    private fun createCard(title: String): Pair<View, CardViews> {
        val value = text("--", 23f, Color.WHITE, true)
        val detail = text("waiting", 12f, Color.rgb(142, 158, 171), false)
        val layout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(dp(12), dp(10), dp(10), dp(10))
            background = rounded(Color.rgb(13, 20, 26), Color.rgb(37, 53, 63))
            addView(text(title, 10f, Color.rgb(82, 224, 180), true))
            addView(value, LinearLayout.LayoutParams(-1, 0, 1f).apply { topMargin = dp(4) })
            addView(detail)
        }
        return layout to CardViews(value, detail)
    }

    private val refreshUi = object : Runnable {
        override fun run() {
            val activeService = service?.takeIf { it.connectionStatus != "Stopped" }
            val snapshot = activeService?.latestSnapshot ?: previewCollector.collect()
            statusText.text = activeService?.connectionStatus
                ?: "Preview • press CONNECT to send over BLE"
            cards[0].value.text = snapshot.batteryPercent?.let { "$it%" } ?: "N/A"
            cards[0].detail.text = when {
                snapshot.full -> "full"
                snapshot.charging -> "charging"
                else -> snapshot.voltageMv?.let { "${it} mV" } ?: "battery API"
            }
            cards[1].value.text = snapshot.powerCentiW?.let { "%.2f W".format(it / 100f) } ?: "N/A"
            cards[1].detail.text = snapshot.chargerTempDeciC?.let {
                "charger %.1f °C".format(it / 10f)
            } ?: snapshot.currentMa?.let { "${kotlin.math.abs(it)} mA estimate" } ?: "current blocked"
            cards[2].value.text = snapshot.batteryTempDeciC?.let { "%.1f °C".format(it / 10f) } ?: "N/A"
            cards[2].detail.text = snapshot.cycleCount?.let { "$it cycles" } ?: "cycles unavailable"
            cards[3].value.text = thermalName(snapshot.thermalStatus)
            cards[3].detail.text = snapshot.thermalHeadroomPercent?.let { "$it% headroom" } ?: "Android thermal API"
            cards[4].value.text = snapshot.cpuUsagePercent?.let { "$it% CPU" } ?: "CPU N/A"
            cards[4].detail.text = listOfNotNull(
                snapshot.cpuMhz?.let { "$it MHz" },
                snapshot.cpuTempDeciC?.let { "%.1f °C".format(it / 10f) },
                snapshot.ramUsagePercent?.let { "$it% RAM" },
            ).joinToString(" • ").ifBlank { "kernel blocked" }
            cards[5].value.text = snapshot.gpuUsagePercent?.let { "$it% GPU" } ?: "GPU N/A"
            cards[5].detail.text = listOfNotNull(
                snapshot.gpuMhz?.let { "$it MHz" },
                snapshot.gpuTempDeciC?.let { "%.1f °C".format(it / 10f) },
            ).joinToString(" • ").ifBlank { "kernel blocked" }

            val available = buildList {
                add("Reliable: battery, charging, battery temperature, RAM, thermal status")
                if (snapshot.cpuMhz == null || snapshot.cpuUsagePercent == null) {
                    add("CPU detail: blocked by this Android kernel (dashboard will show N/A)")
                }
                if (snapshot.gpuMhz == null || snapshot.gpuUsagePercent == null) {
                    add("GPU detail: blocked by this Android kernel (dashboard will show N/A)")
                }
                add(if (snapshot.wifiConnected) {
                    "Wi-Fi: ${snapshot.ssid ?: "connected"} • ${snapshot.wifiRssiDbm ?: "--"} dBm"
                } else "Wi-Fi: unavailable or permission not granted")
                add("BLE GATT links: ${snapshot.bluetoothGattConnections ?: "unavailable"}")
                add("Controller battery: WIRED — the X5 Lite has no internal battery")
                add("ESP battery: NO GAUGE until MAX17048 is added")
            }
            capabilityText.text = available.joinToString("\n\n")
            handler.postDelayed(this, 1000L)
        }
    }

    private fun requestPermissionsAndStart() {
        val missing = requiredPermissions().filter {
            checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED
        }
        if (missing.isNotEmpty()) {
            requestPermissions(missing.toTypedArray(), 42)
        } else {
            startTelemetryService()
        }
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray,
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == 42 && bluetoothPermissionsGranted()) {
            startTelemetryService()
        } else if (requestCode == 42) {
            statusText.text = "Bluetooth permission is required; Wi-Fi permission is optional"
        }
    }

    private fun startTelemetryService() {
        startForegroundService(
            Intent(this, TelemetryService::class.java).setAction(TelemetryService.ACTION_START),
        )
    }

    private fun requiredPermissions(): List<String> = buildList {
        if (Build.VERSION.SDK_INT >= 31) {
            add(Manifest.permission.BLUETOOTH_SCAN)
            add(Manifest.permission.BLUETOOTH_CONNECT)
        } else {
            add(Manifest.permission.ACCESS_FINE_LOCATION)
        }
        if (Build.VERSION.SDK_INT >= 33) {
            add(Manifest.permission.NEARBY_WIFI_DEVICES)
            add(Manifest.permission.POST_NOTIFICATIONS)
        }
    }

    private fun bluetoothPermissionsGranted(): Boolean = if (Build.VERSION.SDK_INT >= 31) {
        checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN) == PackageManager.PERMISSION_GRANTED &&
            checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED
    } else {
        checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED
    }

    private fun thermalName(status: Int?): String = when (status) {
        0 -> "NOMINAL"
        1 -> "LIGHT"
        2 -> "MODERATE"
        3 -> "SEVERE"
        4 -> "CRITICAL"
        5 -> "EMERGENCY"
        6 -> "SHUTDOWN"
        else -> "N/A"
    }

    private fun button(label: String, color: Int, textColor: Int, click: () -> Unit) =
        Button(this).apply {
            text = label
            setTextColor(textColor)
            textSize = 13f
            typeface = Typeface.DEFAULT_BOLD
            background = rounded(color, color)
            setOnClickListener { click() }
        }

    private fun text(value: String, size: Float, color: Int, bold: Boolean) =
        TextView(this).apply {
            text = value
            textSize = size
            setTextColor(color)
            typeface = if (bold) Typeface.DEFAULT_BOLD else Typeface.DEFAULT
        }

    private fun rounded(fill: Int, stroke: Int) = GradientDrawable().apply {
        cornerRadius = dp(12).toFloat()
        setColor(fill)
        setStroke(dp(1), stroke)
    }

    private fun dp(value: Int): Int = (value * resources.displayMetrics.density).toInt()
}
