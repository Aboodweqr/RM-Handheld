#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>

#include "../firmware/main/hid_report_parser.hpp"
#include "../firmware/main/input_router.hpp"
#include "../firmware/main/telemetry_store.hpp"
#include "../protocol/telemetry_protocol.hpp"

namespace {

void protocol_round_trip() {
    rmh::CoreTelemetry input;
    input.battery_percent = 73;
    input.flags = rmh::kCharging | rmh::kHasPower | rmh::kHasBatteryTemperature;
    input.battery_temperature_deci_c = 384;
    input.voltage_mv = 4382;
    input.current_ma = 1725;
    input.power_centi_w = 756;
    input.thermal_status = 2;
    input.thermal_headroom_percent = 61;
    input.cycle_count = 98;

    const auto bytes = rmh::encode(rmh::make_core_frame(input, 17));
    assert(bytes.size() == 20);
    rmh::Frame frame;
    assert(rmh::decode(bytes.data(), bytes.size(), frame));
    assert(frame.sequence == 17);
    rmh::CoreTelemetry output;
    assert(rmh::read_core(frame, output));
    assert(output.battery_percent == 73);
    assert(output.battery_temperature_deci_c == 384);
    assert(output.voltage_mv == 4382);
    assert(output.current_ma == 1725);
    assert(output.power_centi_w == 756);
    assert(output.cycle_count == 98);

    auto damaged = bytes;
    damaged[9] ^= 0x20;
    assert(!rmh::decode(damaged.data(), damaged.size(), frame));
}

void store_history() {
    rmh::TelemetryStore store;
    for (std::uint8_t index = 0; index < 3; ++index) {
        rmh::CoreTelemetry sample;
        sample.battery_percent = static_cast<std::uint8_t>(80 - index);
        sample.battery_temperature_deci_c = static_cast<std::int16_t>(350 + index);
        sample.power_centi_w = static_cast<std::uint16_t>(500 + index);
        const auto bytes = rmh::encode(rmh::make_core_frame(sample, index));
        assert(store.ingest(bytes.data(), bytes.size(), 1000 + index));
    }
    assert(store.battery_history(0) == 78);
    assert(store.battery_history(2) == 80);
    assert(store.temperature_history(1) == 351);
    assert(store.phone_online(2000));
    assert(!store.phone_online(6000));

    rmh::PerformanceTelemetry performance;
    performance.cpu_usage_percent = 44;
    performance.gpu_usage_percent = 61;
    performance.charger_temperature_deci_c = 412;
    auto performanceBytes = rmh::encode(rmh::make_performance_frame(performance, 4));
    assert(store.ingest(performanceBytes.data(), performanceBytes.size(), 2001));
    assert(store.cpu_history(0) == 44);
    assert(store.gpu_history(0) == 61);
    assert(store.charger_temperature_history(0) == 412);

    rmh::NetworkTelemetry network;
    network.wifi_rssi_dbm = -53;
    auto networkBytes = rmh::encode(rmh::make_network_frame(network, 5));
    assert(store.ingest(networkBytes.data(), networkBytes.size(), 2002));
    assert(store.wifi_history(0) == -53);

    rmh::IdentityTelemetry identity;
    identity.flags = 1;
    const char name[] = "Home WiFi";
    std::copy(name, name + sizeof(name), identity.wifi_name.begin());
    auto identityBytes = rmh::encode(rmh::make_identity_frame(identity, 6));
    assert(store.ingest(identityBytes.data(), identityBytes.size(), 2003));
    assert(store.identity().wifi_name[0] == 'H');
}

void standard_hid_descriptor() {
    constexpr std::array<std::uint8_t, 69> descriptor{
        0x05, 0x01, 0x09, 0x05, 0xA1, 0x01, 0x85, 0x01,
        0x05, 0x09, 0x19, 0x01, 0x29, 0x10, 0x15, 0x00,
        0x25, 0x01, 0x75, 0x01, 0x95, 0x10, 0x81, 0x02,
        0x05, 0x01, 0x09, 0x39, 0x15, 0x00, 0x25, 0x07,
        0x75, 0x04, 0x95, 0x01, 0x81, 0x42, 0x75, 0x04,
        0x95, 0x01, 0x81, 0x03, 0x09, 0x30, 0x09, 0x31,
        0x09, 0x33, 0x09, 0x34, 0x15, 0x81, 0x25, 0x7F,
        0x75, 0x08, 0x95, 0x04, 0x81, 0x02, 0xC0
    };
    rmh::HidReportParser parser;
    assert(parser.parse_descriptor(descriptor.data(), descriptor.size()));
    assert(parser.has_report_ids());
    assert(parser.field_count() == 21);  // 16 buttons + hat + four axes.

    constexpr std::array<std::uint8_t, 8> report{
        0x01,  // Report ID.
        0x81, 0x00,  // Buttons 1 and 8.
        0x02,        // Hat right, high nibble padding.
        0x81, 0x7F, 0x00, 0x40  // LX, LY, RX, RY.
    };
    rmh::GamepadState state;
    assert(parser.decode_input(report.data(), report.size(), state));
    assert(state.pressed(rmh::GamepadButton::A));
    assert(state.pressed(rmh::GamepadButton::Menu));
    assert(!state.pressed(rmh::GamepadButton::B));
    assert(state.hat == 2);
    assert(state.left_x < -32000);
    assert(state.left_y > 32000);
    assert(state.right_x > -500 && state.right_x < 500);
    assert(state.right_y > 15000);
}

void dashboard_navigation() {
    rmh::InputRouter router;
    rmh::GamepadState input;
    input.set(rmh::GamepadButton::View, true);
    input.set(rmh::GamepadButton::Menu, true);

    auto result = router.update(input, 100);
    assert(!result.dashboard_active);
    assert(result.forwarded.buttons == 0);
    result = router.update(input, 2101);
    assert(result.dashboard_active);
    assert(result.action == rmh::DashboardAction::Entered);
    assert(result.forwarded.buttons == 0);

    input = {};
    router.update(input, 2110);
    input.hat = 2;
    result = router.update(input, 2120);
    assert(result.selected_card == 1);
    assert(result.action == rmh::DashboardAction::SelectionChanged);

    input.hat = 8;
    router.update(input, 2130);
    input.set(rmh::GamepadButton::A, true);
    result = router.update(input, 2140);
    assert(result.page == rmh::DashboardPage::Battery);
    assert(result.action == rmh::DashboardAction::DetailOpened);

    input = {};
    router.update(input, 2150);
    input.set(rmh::GamepadButton::B, true);
    result = router.update(input, 2160);
    assert(result.page == rmh::DashboardPage::Overview);
    assert(result.action == rmh::DashboardAction::Returned);

    input = {};
    router.update(input, 2170);
    input.hat = 6;
    result = router.update(input, 2180);
    assert(result.selected_card == 0);
    input.hat = 8;
    router.update(input, 2190);
    input.hat = 4;
    result = router.update(input, 2200);
    assert(result.selected_card == 2);
    input.hat = 8;
    router.update(input, 2210);
    input.set(rmh::GamepadButton::A, true);
    result = router.update(input, 2220);
    assert(result.page == rmh::DashboardPage::Temperature);

    input = {};
    router.update(input, 2230);
    input.set(rmh::GamepadButton::B, true);
    result = router.update(input, 2240);
    assert(result.page == rmh::DashboardPage::Overview);
    input = {};
    router.update(input, 2250);
    input.hat = 4;
    result = router.update(input, 2260);
    assert(result.selected_card == 4);
    input.hat = 8;
    router.update(input, 2270);
    input.set(rmh::GamepadButton::A, true);
    result = router.update(input, 2280);
    assert(result.page == rmh::DashboardPage::Performance);

    input = {};
    router.update(input, 2290);
    input.set(rmh::GamepadButton::B, true);
    result = router.update(input, 2300);
    assert(result.page == rmh::DashboardPage::Overview);

    input = {};
    router.update(input, 2310);
    input.hat = 4;
    result = router.update(input, 2320);
    assert(result.selected_card == 6);
    input.hat = 8;
    router.update(input, 2330);
    input.hat = 2;
    result = router.update(input, 2340);
    assert(result.selected_card == 7);
    input.hat = 8;
    router.update(input, 2350);
    input.set(rmh::GamepadButton::A, true);
    result = router.update(input, 2360);
    assert(result.page == rmh::DashboardPage::Network);

    input = {};
    result = router.update(input, 22400);
    assert(!result.dashboard_active);
    assert(result.action == rmh::DashboardAction::Exited);
}

}  // namespace

int main() {
    protocol_round_trip();
    store_history();
    standard_hid_descriptor();
    dashboard_navigation();
    std::cout << "All RM Handheld host tests passed.\n";
    return 0;
}
