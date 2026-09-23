#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "../../protocol/telemetry_protocol.hpp"

namespace rmh {

class TelemetryStore {
public:
    static constexpr std::size_t kHistoryPoints = 60;

    bool ingest(const std::uint8_t* bytes, std::size_t size,
                std::uint32_t received_at_ms);
    void update_link(const LinkTelemetry& link, std::uint32_t received_at_ms);

    [[nodiscard]] const CoreTelemetry& core() const { return core_; }
    [[nodiscard]] const PerformanceTelemetry& performance() const {
        return performance_;
    }
    [[nodiscard]] const NetworkTelemetry& network() const { return network_; }
    [[nodiscard]] const LinkTelemetry& link() const { return link_; }
    [[nodiscard]] const IdentityTelemetry& identity() const { return identity_; }
    [[nodiscard]] std::uint32_t last_phone_update_ms() const {
        return last_phone_update_ms_;
    }
    [[nodiscard]] bool phone_online(std::uint32_t now_ms) const {
        return last_phone_update_ms_ != 0 && now_ms - last_phone_update_ms_ < 3500;
    }

    [[nodiscard]] std::int16_t temperature_history(std::size_t age) const;
    [[nodiscard]] std::int16_t charger_temperature_history(std::size_t age) const;
    [[nodiscard]] std::uint16_t power_history(std::size_t age) const;
    [[nodiscard]] std::uint8_t battery_history(std::size_t age) const;
    [[nodiscard]] std::uint8_t cpu_history(std::size_t age) const;
    [[nodiscard]] std::uint8_t gpu_history(std::size_t age) const;
    [[nodiscard]] std::int8_t wifi_history(std::size_t age) const;

private:
    void record_core_history();
    void record_performance_history();
    void record_network_history();

    CoreTelemetry core_{};
    PerformanceTelemetry performance_{};
    NetworkTelemetry network_{};
    LinkTelemetry link_{};
    IdentityTelemetry identity_{};
    std::array<std::int16_t, kHistoryPoints> temperature_history_{};
    std::array<std::int16_t, kHistoryPoints> charger_temperature_history_{};
    std::array<std::uint16_t, kHistoryPoints> power_history_{};
    std::array<std::uint8_t, kHistoryPoints> battery_history_{};
    std::array<std::uint8_t, kHistoryPoints> cpu_history_{};
    std::array<std::uint8_t, kHistoryPoints> gpu_history_{};
    std::array<std::int8_t, kHistoryPoints> wifi_history_{};
    std::size_t history_head_{0};
    std::size_t history_size_{0};
    std::size_t performance_history_head_{0};
    std::size_t performance_history_size_{0};
    std::size_t network_history_head_{0};
    std::size_t network_history_size_{0};
    std::uint32_t last_phone_update_ms_{0};
};

}  // namespace rmh
