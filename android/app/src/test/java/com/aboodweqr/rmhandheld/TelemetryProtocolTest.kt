package com.aboodweqr.rmhandheld

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Test

class TelemetryProtocolTest {
    @Test
    fun corePacketFitsDefaultBlePayloadAndHasValidCrc() {
        val packet = TelemetryProtocol.core(
            TelemetrySnapshot(
                batteryPercent = 73,
                charging = true,
                batteryTempDeciC = 384,
                voltageMv = 4382,
                currentMa = 1725,
                powerCentiW = 756,
                cycleCount = 98,
                thermalStatus = 2,
                thermalHeadroomPercent = 61,
            ),
            17,
        )
        assertEquals(20, packet.size)
        assertEquals(0xA5, packet[0].toInt() and 0xFF)
        assertEquals(1, packet[1].toInt() and 0xFF)
        assertEquals(1, packet[2].toInt() and 0xFF)
        assertEquals(17, packet[3].toInt() and 0xFF)
        val encodedCrc = (packet[18].toInt() and 0xFF) or
            ((packet[19].toInt() and 0xFF) shl 8)
        assertEquals(TelemetryProtocol.crc16Ccitt(packet, 18), encodedCrc)
        packet[8] = (packet[8].toInt() xor 0x20).toByte()
        assertNotEquals(TelemetryProtocol.crc16Ccitt(packet, 18), encodedCrc)
    }
}
