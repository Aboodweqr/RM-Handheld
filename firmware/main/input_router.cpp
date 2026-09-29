#include "input_router.hpp"

namespace rmh {
namespace {

bool rising(const GamepadState& now, const GamepadState& before,
            GamepadButton button) {
    return now.pressed(button) && !before.pressed(button);
}

bool hat_left(std::uint8_t hat) { return hat == 6 || hat == 5 || hat == 7; }
bool hat_right(std::uint8_t hat) { return hat == 2 || hat == 1 || hat == 3; }
bool hat_up(std::uint8_t hat) { return hat == 0 || hat == 1 || hat == 7; }
bool hat_down(std::uint8_t hat) { return hat == 4 || hat == 3 || hat == 5; }

DashboardPage detail_for_card(std::uint8_t card) {
    switch (card) {
        case 0:
        case 1: return DashboardPage::Battery;
        case 2:
        case 3: return DashboardPage::Temperature;
        case 4:
        case 5:
        case 6: return DashboardPage::Performance;
        case 7: return DashboardPage::Network;
        default: return DashboardPage::Overview;
    }
}

DashboardPage shifted(DashboardPage page, int amount) {
    auto value = static_cast<int>(page);
    const auto count = static_cast<int>(DashboardPage::Count);
    value = (value + amount + count) % count;
    return static_cast<DashboardPage>(value);
}

}  // namespace

void InputRouter::reset() {
    dashboard_active_ = false;
    chord_armed_ = true;
    chord_started_ms_ = 0;
    last_activity_ms_ = 0;
    page_ = DashboardPage::Overview;
    selected_card_ = 0;
    previous_ = {};
}

RouteResult InputRouter::update(const GamepadState& input, std::uint32_t now_ms) {
    RouteResult result;
    result.forwarded = dashboard_active_ ? GamepadState::neutral() : input;
    result.dashboard_active = dashboard_active_;
    result.page = page_;
    result.selected_card = selected_card_;

    const bool chord = input.pressed(GamepadButton::View) &&
                       input.pressed(GamepadButton::Menu);
    // Consume the chord from its first frame so View/Menu cannot open an
    // in-game menu while the user is trying to enter dashboard mode.
    if (chord) result.forwarded = GamepadState::neutral();
    if (!chord) {
        chord_started_ms_ = 0;
        chord_armed_ = true;
    } else if (chord_started_ms_ == 0) {
        chord_started_ms_ = now_ms == 0 ? 1 : now_ms;
    } else if (chord_armed_ && now_ms - chord_started_ms_ >= kChordHoldMs) {
        dashboard_active_ = !dashboard_active_;
        chord_armed_ = false;
        last_activity_ms_ = now_ms;
        page_ = DashboardPage::Overview;
        result.action = dashboard_active_ ? DashboardAction::Entered
                                          : DashboardAction::Exited;
        result.forwarded = GamepadState::neutral();
    }

    if (dashboard_active_ && result.action == DashboardAction::None) {
        result.forwarded = GamepadState::neutral();
        const bool left = hat_left(input.hat) && !hat_left(previous_.hat);
        const bool right = hat_right(input.hat) && !hat_right(previous_.hat);
        const bool up = hat_up(input.hat) && !hat_up(previous_.hat);
        const bool down = hat_down(input.hat) && !hat_down(previous_.hat);
        const bool lb = rising(input, previous_, GamepadButton::LeftBumper);
        const bool rb = rising(input, previous_, GamepadButton::RightBumper);
        const bool a = rising(input, previous_, GamepadButton::A);
        const bool b = rising(input, previous_, GamepadButton::B);
        const bool any = left || right || up || down || lb || rb || a || b;
        if (any) last_activity_ms_ = now_ms;

        if (b && page_ != DashboardPage::Overview) {
            page_ = DashboardPage::Overview;
            result.action = DashboardAction::Returned;
        } else if (page_ == DashboardPage::Overview) {
            if (left && (selected_card_ % 2U) != 0) {
                --selected_card_;
                result.action = DashboardAction::SelectionChanged;
            } else if (right && (selected_card_ % 2U) == 0 && selected_card_ < 7) {
                ++selected_card_;
                result.action = DashboardAction::SelectionChanged;
            } else if (up && selected_card_ >= 2) {
                selected_card_ = static_cast<std::uint8_t>(selected_card_ - 2);
                result.action = DashboardAction::SelectionChanged;
            } else if (down && selected_card_ <= 5) {
                selected_card_ = static_cast<std::uint8_t>(selected_card_ + 2);
                result.action = DashboardAction::SelectionChanged;
            } else if (a) {
                page_ = detail_for_card(selected_card_);
                result.action = DashboardAction::DetailOpened;
            } else if (lb || rb) {
                page_ = rb ? DashboardPage::Battery : DashboardPage::Animation;
                result.action = DashboardAction::PageChanged;
            }
        } else if (left || lb || right || rb) {
            page_ = shifted(page_, (right || rb) ? 1 : -1);
            if (page_ == DashboardPage::Overview) {
                page_ = shifted(page_, (right || rb) ? 1 : -1);
            }
            result.action = DashboardAction::PageChanged;
        }

        if (last_activity_ms_ != 0 && now_ms - last_activity_ms_ >= kIdleTimeoutMs) {
            dashboard_active_ = false;
            page_ = DashboardPage::Overview;
            result.action = DashboardAction::Exited;
        }
    }

    previous_ = input;
    result.dashboard_active = dashboard_active_;
    result.page = page_;
    result.selected_card = selected_card_;
    return result;
}

}  // namespace rmh
