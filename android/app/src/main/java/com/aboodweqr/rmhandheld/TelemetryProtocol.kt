package com.aboodweqr.rmhandheld

import java.nio.ByteBuffer
import java.nio.ByteOrder

object TelemetryProtocol {
    const val MAGIC = 0xA5
    const val VERSION = 1
    const val FRAME_SIZE = 20
    const val UNKNOWN_U8 = 0xFF
    const val UNKNOWN_U16 = 0xFFFF
    const val UNKNOWN_I16 = Short.MIN_VALUE.toInt()

    const val CORE = 1
    const val PERFORMANCE = 2
    const val NETWORK = 3
    const val IDENTITY = 5

    const val CORE_CHARGING = 1 shl 0
    const val CORE_FULL = 1 shl 1
    const val CORE_HAS_CURRENT = 1 shl 2
    const val CORE_HAS_POWER = 1 shl 3
    const val CORE_HAS_BATTERY_TEMP = 1 shl 4
    const val CORE_HAS_CYCLES = 1 shl 5

    const val PERF_HAS_CPU_USAGE = 1 shl 0
    const val PERF_HAS_GPU_USAGE = 1 shl 1
    const val PERF_HAS_CPU_CLOCK = 1 shl 2
    const val PERF_HAS_GPU_CLOCK = 1 shl 3
    const val PERF_HAS_CPU_TEMP = 1 shl 4
    const val PERF_HAS_GPU_TEMP = 1 shl 5
    const val PERF_HAS_CHARGER_TEMP = 1 shl 6

    fun core(snapshot: TelemetrySnapshot, sequence: Int): ByteArray {
        var flags = 0
        if (snapshot.charging) flags = flags or CORE_CHARGING
        if (snapshot.full) flags = flags or CORE_FULL
        if (snapshot.currentMa != null) flags = flags or CORE_HAS_CURRENT
        if (snapshot.powerCentiW != null) flags = flags or CORE_HAS_POWER
        if (snapshot.batteryTempDeciC != null) flags = flags or CORE_HAS_BATTERY_TEMP
        if (snapshot.cycleCount != null) flags = flags or CORE_HAS_CYCLES
        return frame(CORE, sequence) {
            putU8(snapshot.batteryPercent ?: UNKNOWN_U8)
            putU8(flags)
            putI16(snapshot.batteryTempDeciC ?: UNKNOWN_I16)
            putU16(snapshot.voltageMv ?: UNKNOWN_U16)
            putI16(snapshot.currentMa?.coerceIn(Short.MIN_VALUE.toInt(), Short.MAX_VALUE.toInt())
                ?: UNKNOWN_I16)
            putU16(snapshot.powerCentiW?.coerceIn(0, 0xFFFE) ?: UNKNOWN_U16)
            putU8(snapshot.thermalStatus ?: UNKNOWN_U8)
            putU8(snapshot.thermalHeadroomPercent ?: UNKNOWN_U8)
            putU16(snapshot.cycleCount?.coerceIn(0, 0xFFFE) ?: UNKNOWN_U16)
        }
    }

    fun performance(snapshot: TelemetrySnapshot, sequence: Int): ByteArray {
        var flags = 0
        if (snapshot.cpuUsagePercent != null) flags = flags or PERF_HAS_CPU_USAGE
        if (snapshot.gpuUsagePercent != null) flags = flags or PERF_HAS_GPU_USAGE
        if (snapshot.cpuMhz != null) flags = flags or PERF_HAS_CPU_CLOCK
        if (snapshot.gpuMhz != null) flags = flags or PERF_HAS_GPU_CLOCK
        if (snapshot.cpuTempDeciC != null) flags = flags or PERF_HAS_CPU_TEMP
        if (snapshot.gpuTempDeciC != null) flags = flags or PERF_HAS_GPU_TEMP
        if (snapshot.chargerTempDeciC != null) flags = flags or PERF_HAS_CHARGER_TEMP
        return frame(PERFORMANCE, sequence) {
            putU8(flags)
            putU8(snapshot.cpuUsagePercent ?: UNKNOWN_U8)
            putU8(snapshot.gpuUsagePercent ?: UNKNOWN_U8)
            putU8(snapshot.ramUsagePercent ?: UNKNOWN_U8)
            putU16(snapshot.cpuMhz?.coerceIn(0, 0xFFFE) ?: UNKNOWN_U16)
            putU16(snapshot.gpuMhz?.coerceIn(0, 0xFFFE) ?: UNKNOWN_U16)
            putI16(snapshot.cpuTempDeciC ?: UNKNOWN_I16)
            putI16(snapshot.gpuTempDeciC ?: UNKNOWN_I16)
            putI16(snapshot.chargerTempDeciC ?: UNKNOWN_I16)
        }
    }

    fun network(snapshot: TelemetrySnapshot, sequence: Int): ByteArray {
        var flags = 0
        if (snapshot.wifiConnected) flags = flags or 1
        if (snapshot.ssid != null) flags = flags or 2
        return frame(NETWORK, sequence) {
            putU8(flags)
            putU8(snapshot.wifiRssiDbm?.and(0xFF) ?: 0x80)
            putU16(snapshot.rxLinkMbps ?: UNKNOWN_U16)
            putU16(snapshot.txLinkMbps ?: UNKNOWN_U16)
            putU8(snapshot.bluetoothGattConnections ?: UNKNOWN_U8)
            putU8(if (snapshot.wifiConnected) 1 else 0)
            putInt(snapshot.ssid?.let(::fnv1a32) ?: 0)
            putU16(1000)
        }
    }

    fun identity(snapshot: TelemetrySnapshot, sequence: Int): ByteArray {
        return frame(IDENTITY, sequence) {
            putU8(if (snapshot.ssid != null) 1 else 0)
            val safeName = snapshot.ssid.orEmpty().map { character ->
                if (character.code in 32..126) character else '?'
            }.joinToString("").take(13).toByteArray(Charsets.US_ASCII)
            repeat(13) { index ->
                put(if (index < safeName.size) safeName[index] else 0.toByte())
            }
        }
    }

    private fun frame(kind: Int, sequence: Int, payloadWriter: ByteBuffer.() -> Unit): ByteArray {
        val bytes = ByteArray(FRAME_SIZE)
        val buffer = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        buffer.put(MAGIC.toByte())
        buffer.put(VERSION.toByte())
        buffer.put(kind.toByte())
        buffer.put(sequence.toByte())
        val start = buffer.position()
        buffer.payloadWriter()
        check(buffer.position() - start == 14) { "Telemetry payload must be exactly 14 bytes" }
        val crc = crc16Ccitt(bytes, 18)
        buffer.putShort(crc.toShort())
        return bytes
    }

    private fun ByteBuffer.putU8(value: Int) = put(value.toByte())
    private fun ByteBuffer.putU16(value: Int) = putShort(value.toShort())
    private fun ByteBuffer.putI16(value: Int) = putShort(value.toShort())

    fun crc16Ccitt(bytes: ByteArray, size: Int = bytes.size): Int {
        var crc = 0xFFFF
        for (index in 0 until size) {
            crc = crc xor ((bytes[index].toInt() and 0xFF) shl 8)
            repeat(8) {
                crc = if ((crc and 0x8000) != 0) {
                    ((crc shl 1) xor 0x1021) and 0xFFFF
                } else {
                    (crc shl 1) and 0xFFFF
                }
            }
        }
        return crc
    }

    private fun fnv1a32(value: String): Int {
        var hash = 0x811C9DC5.toInt()
        value.toByteArray(Charsets.UTF_8).forEach {
            hash = hash xor (it.toInt() and 0xFF)
            hash *= 0x01000193
        }
        return hash
    }
}
