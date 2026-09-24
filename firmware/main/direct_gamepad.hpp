#pragma once

#include <array>
#include <cstdint>

#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "gamepad_state.hpp"

namespace rmh {

// Reads the X5 Lite controls that are soldered directly to ESP32-S3 GPIOs.
// Sticks are centred during begin(), so leave them untouched while booting.
class DirectGamepad {
public:
    esp_err_t begin();
    GamepadState read();

    [[nodiscard]] bool ready() const { return ready_; }
    [[nodiscard]] std::uint32_t reports_received() const { return samples_; }

private:
    enum class AnalogInput : std::uint8_t {
        LeftX = 0,
        LeftY,
        RightX,
        RightY,
        LeftTrigger,
        RightTrigger,
        Count,
    };

    static constexpr std::size_t kAnalogCount =
        static_cast<std::size_t>(AnalogInput::Count);
    static constexpr std::size_t index_of(AnalogInput input) {
        return static_cast<std::size_t>(input);
    }

    int read_raw(AnalogInput input) const;
    std::int16_t read_stick(AnalogInput input, bool invert);
    std::uint16_t read_trigger(AnalogInput input);

    adc_oneshot_unit_handle_t adc1_{nullptr};
    adc_oneshot_unit_handle_t adc2_{nullptr};
    std::array<int, kAnalogCount> center_{};
    std::array<int, kAnalogCount> filtered_{};
    std::array<int, kAnalogCount> trigger_peak_{};
    std::array<bool, kAnalogCount> analog_valid_{};
    bool ready_{false};
    std::uint32_t samples_{0};
};

}  // namespace rmh
