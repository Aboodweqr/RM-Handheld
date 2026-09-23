#include "telemetry_store.hpp"

namespace rmh {

bool TelemetryStore::ingest(const std::uint8_t* bytes, std::size_t size,
                            std::uint32_t received_at_ms) {
    Frame frame;
    if (!decode(bytes, size, frame)) return false;
    switch (frame.kind) {
        case FrameKind::Core:
            if (!read_core(frame, core_)) return false;
            record_core_history();
            break;
        case FrameKind::Performance:
            if (!read_performance(frame, performance_)) return false;
            record_performance_history();
            break;
        case FrameKind::Network:
            if (!read_network(frame, network_)) return false;
            record_network_history();
            break;
        case FrameKind::Link:
            if (!read_link(frame, link_)) return false;
            break;
        case FrameKind::Identity:
            if (!read_identity(frame, identity_)) return false;
            break;
        default: return false;
    }
    last_phone_update_ms_ = received_at_ms;
    return true;
}

void TelemetryStore::update_link(const LinkTelemetry& link,
                                 std::uint32_t received_at_ms) {
    link_ = link;
    (void)received_at_ms;
}

void TelemetryStore::record_core_history() {
    temperature_history_[history_head_] = core_.battery_temperature_deci_c;
    power_history_[history_head_] = core_.power_centi_w;
    battery_history_[history_head_] = core_.battery_percent;
    history_head_ = (history_head_ + 1U) % kHistoryPoints;
    if (history_size_ < kHistoryPoints) ++history_size_;
}

void TelemetryStore::record_performance_history() {
    cpu_history_[performance_history_head_] = performance_.cpu_usage_percent;
    gpu_history_[performance_history_head_] = performance_.gpu_usage_percent;
    charger_temperature_history_[performance_history_head_] =
        performance_.charger_temperature_deci_c;
    performance_history_head_ =
        (performance_history_head_ + 1U) % kHistoryPoints;
    if (performance_history_size_ < kHistoryPoints) ++performance_history_size_;
}

void TelemetryStore::record_network_history() {
    wifi_history_[network_history_head_] = network_.wifi_rssi_dbm;
    network_history_head_ = (network_history_head_ + 1U) % kHistoryPoints;
    if (network_history_size_ < kHistoryPoints) ++network_history_size_;
}

std::int16_t TelemetryStore::temperature_history(std::size_t age) const {
    if (age >= history_size_) return kUnknownSigned16;
    const auto index = (history_head_ + kHistoryPoints - 1U - age) % kHistoryPoints;
    return temperature_history_[index];
}

std::int16_t TelemetryStore::charger_temperature_history(std::size_t age) const {
    if (age >= performance_history_size_) return kUnknownSigned16;
    const auto index =
        (performance_history_head_ + kHistoryPoints - 1U - age) % kHistoryPoints;
    return charger_temperature_history_[index];
}

std::uint16_t TelemetryStore::power_history(std::size_t age) const {
    if (age >= history_size_) return kUnknown16;
    const auto index = (history_head_ + kHistoryPoints - 1U - age) % kHistoryPoints;
    return power_history_[index];
}

std::uint8_t TelemetryStore::battery_history(std::size_t age) const {
    if (age >= history_size_) return kUnknown8;
    const auto index = (history_head_ + kHistoryPoints - 1U - age) % kHistoryPoints;
    return battery_history_[index];
}

std::uint8_t TelemetryStore::cpu_history(std::size_t age) const {
    if (age >= performance_history_size_) return kUnknown8;
    const auto index = (performance_history_head_ + kHistoryPoints - 1U - age) %
                       kHistoryPoints;
    return cpu_history_[index];
}

std::uint8_t TelemetryStore::gpu_history(std::size_t age) const {
    if (age >= performance_history_size_) return kUnknown8;
    const auto index = (performance_history_head_ + kHistoryPoints - 1U - age) %
                       kHistoryPoints;
    return gpu_history_[index];
}

std::int8_t TelemetryStore::wifi_history(std::size_t age) const {
    if (age >= network_history_size_) return INT8_MIN;
    const auto index = (network_history_head_ + kHistoryPoints - 1U - age) %
                       kHistoryPoints;
    return wifi_history_[index];
}

}  // namespace rmh
