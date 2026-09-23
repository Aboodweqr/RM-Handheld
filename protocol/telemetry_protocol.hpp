#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace rmh {

constexpr std::uint8_t kTelemetryMagic = 0xA5;
constexpr std::uint8_t kProtocolVersion = 1;
constexpr std::size_t kPayloadSize = 14;
constexpr std::size_t kFrameSize = 20;
constexpr std::uint8_t kUnknown8 = 0xFF;
constexpr std::uint16_t kUnknown16 = 0xFFFF;
constexpr std::int16_t kUnknownSigned16 = INT16_MIN;

enum class FrameKind : std::uint8_t {
    Core = 1,
    Performance = 2,
    Network = 3,
    Link = 4,
    Identity = 5,
};

enum CoreFlags : std::uint8_t {
    kCharging = 1U << 0,
    kFull = 1U << 1,
    kHasCurrent = 1U << 2,
    kHasPower = 1U << 3,
    kHasBatteryTemperature = 1U << 4,
    kHasCycles = 1U << 5,
};

enum PerformanceFlags : std::uint8_t {
    kHasCpuUsage = 1U << 0,
    kHasGpuUsage = 1U << 1,
    kHasCpuClock = 1U << 2,
    kHasGpuClock = 1U << 3,
    kHasCpuTemperature = 1U << 4,
    kHasGpuTemperature = 1U << 5,
    kHasChargerTemperature = 1U << 6,
};

struct CoreTelemetry {
    std::uint8_t battery_percent{kUnknown8};
    std::uint8_t flags{0};
    std::int16_t battery_temperature_deci_c{kUnknownSigned16};
    std::uint16_t voltage_mv{kUnknown16};
    std::int16_t current_ma{kUnknownSigned16};
    std::uint16_t power_centi_w{kUnknown16};
    std::uint8_t thermal_status{kUnknown8};
    std::uint8_t thermal_headroom_percent{kUnknown8};
    std::uint16_t cycle_count{kUnknown16};
};

struct PerformanceTelemetry {
    std::uint8_t flags{0};
    std::uint8_t cpu_usage_percent{kUnknown8};
    std::uint8_t gpu_usage_percent{kUnknown8};
    std::uint8_t ram_usage_percent{kUnknown8};
    std::uint16_t cpu_mhz{kUnknown16};
    std::uint16_t gpu_mhz{kUnknown16};
    std::int16_t cpu_temperature_deci_c{kUnknownSigned16};
    std::int16_t gpu_temperature_deci_c{kUnknownSigned16};
    std::int16_t charger_temperature_deci_c{kUnknownSigned16};
};

struct NetworkTelemetry {
    std::uint8_t flags{0};
    std::int8_t wifi_rssi_dbm{INT8_MIN};
    std::uint16_t receive_link_mbps{kUnknown16};
    std::uint16_t transmit_link_mbps{kUnknown16};
    std::uint8_t bluetooth_connection_count{kUnknown8};
    std::uint8_t connection_flags{0};
    std::uint32_t ssid_hash{0};
    std::uint16_t sample_interval_ms{1000};
};

struct LinkTelemetry {
    std::uint8_t controller_battery_percent{kUnknown8};
    std::uint8_t handheld_battery_percent{kUnknown8};
    std::int8_t ble_rssi_dbm{INT8_MIN};
    std::uint8_t flags{0};
    std::uint16_t usb_vendor_id{0};
    std::uint16_t usb_product_id{0};
    std::uint32_t reports_received{0};
    std::uint16_t firmware_version{0x0100};
};

struct IdentityTelemetry {
    std::uint8_t flags{0};
    std::array<char, 13> wifi_name{};
};

struct Frame {
    std::uint8_t magic{kTelemetryMagic};
    std::uint8_t version{kProtocolVersion};
    FrameKind kind{FrameKind::Core};
    std::uint8_t sequence{0};
    std::array<std::uint8_t, kPayloadSize> payload{};
    std::uint16_t crc{0};
};

std::uint16_t crc16_ccitt(const std::uint8_t* data, std::size_t size);
std::array<std::uint8_t, kFrameSize> encode(const Frame& frame);
bool decode(const std::uint8_t* data, std::size_t size, Frame& frame);

Frame make_core_frame(const CoreTelemetry& value, std::uint8_t sequence);
Frame make_performance_frame(const PerformanceTelemetry& value,
                             std::uint8_t sequence);
Frame make_network_frame(const NetworkTelemetry& value, std::uint8_t sequence);
Frame make_link_frame(const LinkTelemetry& value, std::uint8_t sequence);
Frame make_identity_frame(const IdentityTelemetry& value, std::uint8_t sequence);

bool read_core(const Frame& frame, CoreTelemetry& value);
bool read_performance(const Frame& frame, PerformanceTelemetry& value);
bool read_network(const Frame& frame, NetworkTelemetry& value);
bool read_link(const Frame& frame, LinkTelemetry& value);
bool read_identity(const Frame& frame, IdentityTelemetry& value);

}  // namespace rmh
