#pragma once

#include <cstdint>

#include "gamepad_state.hpp"

namespace rmh {

enum class DashboardPage : std::uint8_t {
    Overview = 0,
    Battery,
    Temperature,
    Performance,
    Network,
    Controller,
    Animation,
    Count,
};

enum class DashboardAction : std::uint8_t {
    None,
    Entered,
    Exited,
    PageChanged,
    SelectionChanged,
    DetailOpened,
    Returned,
};

struct RouteResult {
    GamepadState forwarded{};
    DashboardAction action{DashboardAction::None};
    DashboardPage page{DashboardPage::Overview};
    std::uint8_t selected_card{0};
    bool dashboard_active{false};
};

class InputRouter {
public:
    RouteResult update(const GamepadState& input, std::uint32_t now_ms);
    void reset();

    [[nodiscard]] bool dashboard_active() const { return dashboard_active_; }
    [[nodiscard]] DashboardPage page() const { return page_; }
    [[nodiscard]] std::uint8_t selected_card() const { return selected_card_; }

private:
    static constexpr std::uint32_t kChordHoldMs = 2000;
    static constexpr std::uint32_t kIdleTimeoutMs = 20000;

    bool dashboard_active_{false};
    bool chord_armed_{true};
    std::uint32_t chord_started_ms_{0};
    std::uint32_t last_activity_ms_{0};
    DashboardPage page_{DashboardPage::Overview};
    std::uint8_t selected_card_{0};
    GamepadState previous_{};
};

}  // namespace rmh
