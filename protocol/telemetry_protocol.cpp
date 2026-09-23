#include "telemetry_protocol.hpp"

#include <algorithm>

namespace rmh {
namespace {

void put_u16(std::array<std::uint8_t, kPayloadSize>& out, std::size_t offset,
             std::uint16_t value) {
    out[offset] = static_cast<std::uint8_t>(value & 0xFFU);
    out[offset + 1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

void put_i16(std::array<std::uint8_t, kPayloadSize>& out, std::size_t offset,
             std::int16_t value) {
    put_u16(out, offset, static_cast<std::uint16_t>(value));
}

void put_u32(std::array<std::uint8_t, kPayloadSize>& out, std::size_t offset,
             std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        out[offset + index] =
            static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU);
    }
}

std::uint16_t get_u16(const std::array<std::uint8_t, kPayloadSize>& in,
                      std::size_t offset) {
    return static_cast<std::uint16_t>(in[offset]) |
           (static_cast<std::uint16_t>(in[offset + 1]) << 8U);
}

std::int16_t get_i16(const std::array<std::uint8_t, kPayloadSize>& in,
                     std::size_t offset) {
    return static_cast<std::int16_t>(get_u16(in, offset));
}

std::uint32_t get_u32(const std::array<std::uint8_t, kPayloadSize>& in,
                      std::size_t offset) {
    std::uint32_t result = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        result |= static_cast<std::uint32_t>(in[offset + index])
                  << (index * 8U);
    }
    return result;
}

Frame make_empty(FrameKind kind, std::uint8_t sequence) {
    Frame frame;
    frame.kind = kind;
    frame.sequence = sequence;
    return frame;
}

}  // namespace

std::uint16_t crc16_ccitt(const std::uint8_t* data, std::size_t size) {
    std::uint16_t crc = 0xFFFFU;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= static_cast<std::uint16_t>(data[index]) << 8U;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000U) != 0
                      ? static_cast<std::uint16_t>((crc << 1U) ^ 0x1021U)
                      : static_cast<std::uint16_t>(crc << 1U);
        }
    }
    return crc;
}

std::array<std::uint8_t, kFrameSize> encode(const Frame& frame) {
    std::array<std::uint8_t, kFrameSize> result{};
    result[0] = kTelemetryMagic;
    result[1] = kProtocolVersion;
    result[2] = static_cast<std::uint8_t>(frame.kind);
    result[3] = frame.sequence;
    std::copy(frame.payload.begin(), frame.payload.end(), result.begin() + 4);
    const auto crc = crc16_ccitt(result.data(), kFrameSize - 2);
    result[18] = static_cast<std::uint8_t>(crc & 0xFFU);
    result[19] = static_cast<std::uint8_t>((crc >> 8U) & 0xFFU);
    return result;
}

bool decode(const std::uint8_t* data, std::size_t size, Frame& frame) {
    if (data == nullptr || size != kFrameSize || data[0] != kTelemetryMagic ||
        data[1] != kProtocolVersion) {
        return false;
    }
    const auto received_crc = static_cast<std::uint16_t>(data[18]) |
                              (static_cast<std::uint16_t>(data[19]) << 8U);
    if (crc16_ccitt(data, kFrameSize - 2) != received_crc) {
        return false;
    }
    frame.magic = data[0];
    frame.version = data[1];
    frame.kind = static_cast<FrameKind>(data[2]);
    frame.sequence = data[3];
    std::copy(data + 4, data + 18, frame.payload.begin());
    frame.crc = received_crc;
    return true;
}

Frame make_core_frame(const CoreTelemetry& value, std::uint8_t sequence) {
    auto frame = make_empty(FrameKind::Core, sequence);
    frame.payload[0] = value.battery_percent;
    frame.payload[1] = value.flags;
    put_i16(frame.payload, 2, value.battery_temperature_deci_c);
    put_u16(frame.payload, 4, value.voltage_mv);
    put_i16(frame.payload, 6, value.current_ma);
    put_u16(frame.payload, 8, value.power_centi_w);
    frame.payload[10] = value.thermal_status;
    frame.payload[11] = value.thermal_headroom_percent;
    put_u16(frame.payload, 12, value.cycle_count);
    return frame;
}

Frame make_performance_frame(const PerformanceTelemetry& value,
                             std::uint8_t sequence) {
    auto frame = make_empty(FrameKind::Performance, sequence);
    frame.payload[0] = value.flags;
    frame.payload[1] = value.cpu_usage_percent;
    frame.payload[2] = value.gpu_usage_percent;
    frame.payload[3] = value.ram_usage_percent;
    put_u16(frame.payload, 4, value.cpu_mhz);
    put_u16(frame.payload, 6, value.gpu_mhz);
    put_i16(frame.payload, 8, value.cpu_temperature_deci_c);
    put_i16(frame.payload, 10, value.gpu_temperature_deci_c);
    put_i16(frame.payload, 12, value.charger_temperature_deci_c);
    return frame;
}

Frame make_network_frame(const NetworkTelemetry& value, std::uint8_t sequence) {
    auto frame = make_empty(FrameKind::Network, sequence);
    frame.payload[0] = value.flags;
    frame.payload[1] = static_cast<std::uint8_t>(value.wifi_rssi_dbm);
    put_u16(frame.payload, 2, value.receive_link_mbps);
    put_u16(frame.payload, 4, value.transmit_link_mbps);
    frame.payload[6] = value.bluetooth_connection_count;
    frame.payload[7] = value.connection_flags;
    put_u32(frame.payload, 8, value.ssid_hash);
    put_u16(frame.payload, 12, value.sample_interval_ms);
    return frame;
}

Frame make_link_frame(const LinkTelemetry& value, std::uint8_t sequence) {
    auto frame = make_empty(FrameKind::Link, sequence);
    frame.payload[0] = value.controller_battery_percent;
    frame.payload[1] = value.handheld_battery_percent;
    frame.payload[2] = static_cast<std::uint8_t>(value.ble_rssi_dbm);
    frame.payload[3] = value.flags;
    put_u16(frame.payload, 4, value.usb_vendor_id);
    put_u16(frame.payload, 6, value.usb_product_id);
    put_u32(frame.payload, 8, value.reports_received);
    put_u16(frame.payload, 12, value.firmware_version);
    return frame;
}

Frame make_identity_frame(const IdentityTelemetry& value, std::uint8_t sequence) {
    auto frame = make_empty(FrameKind::Identity, sequence);
    frame.payload[0] = value.flags;
    for (std::size_t index = 0; index < value.wifi_name.size(); ++index) {
        frame.payload[index + 1] = static_cast<std::uint8_t>(value.wifi_name[index]);
    }
    return frame;
}

bool read_core(const Frame& frame, CoreTelemetry& value) {
    if (frame.kind != FrameKind::Core) return false;
    value.battery_percent = frame.payload[0];
    value.flags = frame.payload[1];
    value.battery_temperature_deci_c = get_i16(frame.payload, 2);
    value.voltage_mv = get_u16(frame.payload, 4);
    value.current_ma = get_i16(frame.payload, 6);
    value.power_centi_w = get_u16(frame.payload, 8);
    value.thermal_status = frame.payload[10];
    value.thermal_headroom_percent = frame.payload[11];
    value.cycle_count = get_u16(frame.payload, 12);
    return true;
}

bool read_performance(const Frame& frame, PerformanceTelemetry& value) {
    if (frame.kind != FrameKind::Performance) return false;
    value.flags = frame.payload[0];
    value.cpu_usage_percent = frame.payload[1];
    value.gpu_usage_percent = frame.payload[2];
    value.ram_usage_percent = frame.payload[3];
    value.cpu_mhz = get_u16(frame.payload, 4);
    value.gpu_mhz = get_u16(frame.payload, 6);
    value.cpu_temperature_deci_c = get_i16(frame.payload, 8);
    value.gpu_temperature_deci_c = get_i16(frame.payload, 10);
    value.charger_temperature_deci_c = get_i16(frame.payload, 12);
    return true;
}

bool read_network(const Frame& frame, NetworkTelemetry& value) {
    if (frame.kind != FrameKind::Network) return false;
    value.flags = frame.payload[0];
    value.wifi_rssi_dbm = static_cast<std::int8_t>(frame.payload[1]);
    value.receive_link_mbps = get_u16(frame.payload, 2);
    value.transmit_link_mbps = get_u16(frame.payload, 4);
    value.bluetooth_connection_count = frame.payload[6];
    value.connection_flags = frame.payload[7];
    value.ssid_hash = get_u32(frame.payload, 8);
    value.sample_interval_ms = get_u16(frame.payload, 12);
    return true;
}

bool read_link(const Frame& frame, LinkTelemetry& value) {
    if (frame.kind != FrameKind::Link) return false;
    value.controller_battery_percent = frame.payload[0];
    value.handheld_battery_percent = frame.payload[1];
    value.ble_rssi_dbm = static_cast<std::int8_t>(frame.payload[2]);
    value.flags = frame.payload[3];
    value.usb_vendor_id = get_u16(frame.payload, 4);
    value.usb_product_id = get_u16(frame.payload, 6);
    value.reports_received = get_u32(frame.payload, 8);
    value.firmware_version = get_u16(frame.payload, 12);
    return true;
}

bool read_identity(const Frame& frame, IdentityTelemetry& value) {
    if (frame.kind != FrameKind::Identity) return false;
    value.flags = frame.payload[0];
    for (std::size_t index = 0; index < value.wifi_name.size(); ++index) {
        value.wifi_name[index] = static_cast<char>(frame.payload[index + 1]);
    }
    return true;
}

}  // namespace rmh
