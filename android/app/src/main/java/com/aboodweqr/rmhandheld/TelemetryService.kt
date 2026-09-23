package com.aboodweqr.rmhandheld

import android.Manifest
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattService
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Binder
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.ParcelUuid
import java.util.UUID

class TelemetryService : Service() {
    companion object {
        const val ACTION_START = "com.aboodweqr.rmhandheld.START"
        const val ACTION_STOP = "com.aboodweqr.rmhandheld.STOP"
        private const val CHANNEL_ID = "rm_handheld_link"
        private const val NOTIFICATION_ID = 61
        private const val DEVICE_NAME = "RM Handheld"
        val SERVICE_UUID: UUID = UUID.fromString("7f510000-1b15-4e6e-9a6b-44524d48444c")
        val TELEMETRY_UUID: UUID = UUID.fromString("7f510001-1b15-4e6e-9a6b-44524d48444c")
    }

    inner class LocalBinder : Binder() {
        val service: TelemetryService get() = this@TelemetryService
    }

    private val binder = LocalBinder()
    private val handler = Handler(Looper.getMainLooper())
    private lateinit var collector: TelemetryCollector
    private var gatt: BluetoothGatt? = null
    private var telemetryCharacteristic: BluetoothGattCharacteristic? = null
    private var running = false
    private var scanning = false
    private var sequence = 0

    @Volatile
    var connectionStatus: String = "Stopped"
        private set

    @Volatile
    var latestSnapshot: TelemetrySnapshot = TelemetrySnapshot()
        private set

    override fun onCreate() {
        super.onCreate()
        collector = TelemetryCollector(applicationContext)
        createNotificationChannel()
    }

    override fun onBind(intent: Intent?): IBinder = binder

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_STOP -> stopLink()
            else -> startLink()
        }
        return START_STICKY
    }

    private fun startLink() {
        if (running) return
        running = true
        startForeground(NOTIFICATION_ID, notification("Starting…"))
        updateStatus("Scanning for RM Handheld service")
        startScanning()
        handler.post(telemetryTick)
    }

    private fun stopLink() {
        running = false
        handler.removeCallbacksAndMessages(null)
        stopScanning()
        telemetryCharacteristic = null
        runCatching { gatt?.disconnect() }
        runCatching { gatt?.close() }
        gatt = null
        connectionStatus = "Stopped"
        stopForeground(STOP_FOREGROUND_REMOVE)
        stopSelf()
    }

    override fun onDestroy() {
        running = false
        handler.removeCallbacksAndMessages(null)
        stopScanning()
        runCatching { gatt?.close() }
        super.onDestroy()
    }

    private val telemetryTick = object : Runnable {
        override fun run() {
            if (!running) return
            latestSnapshot = collector.collect()
            val currentSequence = sequence
            val packets = arrayOf(
                TelemetryProtocol.core(latestSnapshot, currentSequence),
                TelemetryProtocol.performance(latestSnapshot, currentSequence + 1),
                TelemetryProtocol.network(latestSnapshot, currentSequence + 2),
                TelemetryProtocol.identity(latestSnapshot, currentSequence + 3),
            )
            sequence = (sequence + packets.size) and 0xFF
            packets.forEachIndexed { index, bytes ->
                handler.postDelayed({ write(bytes) }, index * 80L)
            }
            handler.postDelayed(this, 1000L)
        }
    }

    private fun startScanning() {
        if (!running || scanning || gatt != null) return
        if (!hasBluetoothPermission()) {
            updateStatus("Bluetooth permission needed")
            handler.postDelayed(::startScanning, 2500L)
            return
        }
        val adapter = getSystemService(BluetoothManager::class.java).adapter
        if (adapter == null || !adapter.isEnabled) {
            updateStatus("Turn Bluetooth on")
            handler.postDelayed(::startScanning, 2500L)
            return
        }
        // Always scan for a live advertisement. A bonded entry can outlive a
        // firmware reflash and otherwise traps the app reconnecting to an old
        // BLE address instead of the board in front of the user.
        val scanner = adapter.bluetoothLeScanner ?: run {
            updateStatus("BLE scanner unavailable")
            return
        }
        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()
        scanning = true
        scanner.startScan(null, settings, scanCallback)
        handler.postDelayed({
            if (scanning) {
                stopScanning()
                updateStatus("Not found; scanning again")
                handler.postDelayed(::startScanning, 1200L)
            }
        }, 12_000L)
    }

    private fun stopScanning() {
        if (!scanning || !hasBluetoothPermission()) return
        val scanner = getSystemService(BluetoothManager::class.java)
            .adapter?.bluetoothLeScanner
        runCatching { scanner?.stopScan(scanCallback) }
        scanning = false
    }

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            val advertisedName = result.scanRecord?.deviceName
            val hasTelemetryService = result.scanRecord?.serviceUuids
                ?.contains(ParcelUuid(SERVICE_UUID)) == true
            val deviceName = if (hasBluetoothPermission()) {
                runCatching { result.device.name }.getOrNull()
            } else null
            if (hasTelemetryService || advertisedName == DEVICE_NAME || deviceName == DEVICE_NAME) {
                stopScanning()
                connect(result.device)
            }
        }

        override fun onScanFailed(errorCode: Int) {
            scanning = false
            updateStatus("BLE scan error $errorCode")
            if (running) handler.postDelayed(::startScanning, 2500L)
        }
    }

    private fun connect(device: BluetoothDevice) {
        if (!hasBluetoothPermission()) return
        updateStatus("Connecting…")
        gatt = device.connectGatt(this, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
    }

    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            handler.post {
                when (newState) {
                    BluetoothProfile.STATE_CONNECTED -> {
                        updateStatus("Discovering dashboard service…")
                        if (hasBluetoothPermission()) gatt.discoverServices()
                    }
                    BluetoothProfile.STATE_DISCONNECTED -> {
                        telemetryCharacteristic = null
                        runCatching { gatt.close() }
                        if (this@TelemetryService.gatt === gatt) this@TelemetryService.gatt = null
                        updateStatus("Disconnected; reconnecting")
                        if (running) handler.postDelayed(::startScanning, 1500L)
                    }
                }
            }
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            handler.post {
                if (status != BluetoothGatt.GATT_SUCCESS) {
                    updateStatus("Service discovery failed; reconnecting")
                    if (hasBluetoothPermission()) gatt.disconnect()
                    return@post
                }
                telemetryCharacteristic = gatt.getService(SERVICE_UUID)
                    ?.getCharacteristic(TELEMETRY_UUID)
                if (telemetryCharacteristic == null) {
                    updateStatus("Firmware telemetry service missing")
                } else {
                    updateStatus("Connected • sending once per second")
                }
            }
        }
    }

    @Suppress("DEPRECATION")
    private fun write(bytes: ByteArray) {
        val activeGatt = gatt ?: return
        val characteristic = telemetryCharacteristic ?: return
        if (!hasBluetoothPermission()) return
        if (Build.VERSION.SDK_INT >= 33) {
            activeGatt.writeCharacteristic(
                characteristic,
                bytes,
                BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE,
            )
        } else {
            characteristic.writeType = BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
            characteristic.value = bytes
            activeGatt.writeCharacteristic(characteristic)
        }
    }

    private fun hasBluetoothPermission(): Boolean =
        Build.VERSION.SDK_INT < 31 ||
            checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED &&
            checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN) == PackageManager.PERMISSION_GRANTED

    private fun updateStatus(status: String) {
        connectionStatus = status
        getSystemService(NotificationManager::class.java)
            .notify(NOTIFICATION_ID, notification(status))
    }

    private fun createNotificationChannel() {
        val manager = getSystemService(NotificationManager::class.java)
        manager.createNotificationChannel(
            NotificationChannel(
                CHANNEL_ID,
                getString(R.string.service_channel),
                NotificationManager.IMPORTANCE_LOW,
            ),
        )
    }

    private fun notification(status: String): Notification {
        val openIntent = Intent(this, MainActivity::class.java)
        val pending = PendingIntent.getActivity(
            this,
            0,
            openIntent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
        return Notification.Builder(this, CHANNEL_ID)
            .setSmallIcon(R.drawable.ic_handheld)
            .setContentTitle("RM Handheld")
            .setContentText(status)
            .setContentIntent(pending)
            .setOngoing(true)
            .build()
    }
}
