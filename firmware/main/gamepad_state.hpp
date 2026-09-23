#pragma once

#include <cstdint>

namespace rmh {

enum class GamepadButton : std::uint8_t {
    A = 0,
    B = 1,
    X = 2,
    Y = 3,
    LeftBumper = 4,
    RightBumper = 5,
    View = 6,
    Menu = 7,
    LeftStick = 8,
    RightStick = 9,
    Home = 10,
};

struct GamepadState {
    std::uint32_t buttons{0};
    std::int16_t left_x{0};
    std::int16_t left_y{0};
    std::int16_t right_x{0};
    std::int16_t right_y{0};
    std::uint16_t left_trigger{0};
    std::uint16_t right_trigger{0};
    std::uint8_t hat{8};  // 0..7 clockwise from up; 8 is neutral.

    [[nodiscard]] bool pressed(GamepadButton button) const {
        return (buttons & (1UL << static_cast<std::uint8_t>(button))) != 0;
    }

    void set(GamepadButton button, bool down) {
        const auto mask = 1UL << static_cast<std::uint8_t>(button);
        buttons = down ? (buttons | mask) : (buttons & ~mask);
    }

    static GamepadState neutral() { return {}; }
};

}  // namespace rmh
