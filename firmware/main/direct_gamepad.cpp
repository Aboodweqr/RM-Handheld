#include "direct_gamepad.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <utility>

#include "board_config.hpp"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace rmh {
namespace {

constexpr char kTag[] = "rmh_direct_pad";
constexpr int kAdcMaximum = 4095;
constexpr int kStickDeadZone = 90;
constexpr int kStickSpan = 1500;
constexpr int kTriggerNoiseFloor = 45;
constexpr int kInitialTriggerSpan = 1200;
constexpr int kStickCenterMinimum = 800;
constexpr int kStickCenterMaximum = 3000;
constexpr int kTriggerRestLowMaximum = 300;
constexpr int kTriggerRestHighMinimum = 3800;
constexpr int kCalibrationNoiseMaximum = 120;
constexpr int kAnalogLogThreshold = 250;

struct ButtonBinding {
    gpio_num_t pin;
    GamepadButton button;
};

constexpr std::array<ButtonBinding, 11> kButtons{{
    {board::kButtonA, GamepadButton::A},
    {board::kButtonB, GamepadButton::B},
    {board::kButtonX, GamepadButton::X},
    {board::kButtonY, GamepadButton::Y},
    {board::kButtonLeftBumper, GamepadButton::LeftBumper},
    {board::kButtonRightBumper, GamepadButton::RightBumper},
    {board::kButtonView, GamepadButton::View},
    {board::kButtonMenu, GamepadButton::Menu},
    {board::kButtonLeftStick, GamepadButton::LeftStick},
    {board::kButtonRightStick, GamepadButton::RightStick},
    {board::kButtonHome, GamepadButton::Home},
}};

constexpr std::array<gpio_num_t, 15> kDigitalPins{{
    board::kButtonA,
    board::kButtonB,
    board::kButtonX,
    board::kButtonY,
    board::kButtonLeftBumper,
    board::kButtonRightBumper,
    board::kButtonDpadUp,
    board::kButtonDpadDown,
    board::kButtonDpadLeft,
    board::kButtonDpadRight,
    board::kButtonLeftStick,
    board::kButtonRightStick,
    board::kButtonView,
    board::kButtonMenu,
    board::kButtonHome,
}};

bool pressed(gpio_num_t pin) {
    return gpio_get_level(pin) == 0;
}

std::uint8_t read_hat() {
    const int horizontal = (pressed(board::kButtonDpadRight) ? 1 : 0) -
                           (pressed(board::kButtonDpadLeft) ? 1 : 0);
    const int vertical = (pressed(board::kButtonDpadDown) ? 1 : 0) -
                         (pressed(board::kButtonDpadUp) ? 1 : 0);
    if (horizontal == 0 && vertical < 0) return 0;
    if (horizontal > 0 && vertical < 0) return 1;
    if (horizontal > 0 && vertical == 0) return 2;
    if (horizontal > 0 && vertical > 0) return 3;
    if (horizontal == 0 && vertical > 0) return 4;
    if (horizontal < 0 && vertical > 0) return 5;
    if (horizontal < 0 && vertical == 0) return 6;
    if (horizontal < 0 && vertical < 0) return 7;
    return 8;
}

}  // namespace

esp_err_t DirectGamepad::begin() {
    std::uint64_t pin_mask = 0;
    for (const auto pin : kDigitalPins) {
        pin_mask |= 1ULL << static_cast<unsigned>(pin);
    }
    const gpio_config_t digital_config = {
        .pin_bit_mask = pin_mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t error = gpio_config(&digital_config);
    if (error != ESP_OK) return error;

    const adc_oneshot_unit_init_cfg_t adc1_config = {
        .unit_id = ADC_UNIT_1,
        .clk_src = static_cast<adc_oneshot_clk_src_t>(0),
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    error = adc_oneshot_new_unit(&adc1_config, &adc1_);
    if (error != ESP_OK) {
        ESP_LOGW(kTag, "ADC1 unavailable: %s", esp_err_to_name(error));
        adc1_ = nullptr;
    }

    const adc_oneshot_unit_init_cfg_t adc2_config = {
        .unit_id = ADC_UNIT_2,
        .clk_src = static_cast<adc_oneshot_clk_src_t>(0),
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    error = adc_oneshot_new_unit(&adc2_config, &adc2_);
    if (error != ESP_OK) {
        ESP_LOGW(kTag, "ADC2 unavailable: %s", esp_err_to_name(error));
        adc2_ = nullptr;
    }

    const adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    if (adc1_ != nullptr &&
        adc_oneshot_config_channel(adc1_, ADC_CHANNEL_0, &channel_config) == ESP_OK) {
        analog_valid_[index_of(AnalogInput::LeftX)] = true;
    }
    if (adc2_ != nullptr) {
        const std::array<std::pair<AnalogInput, adc_channel_t>, 5> adc2_inputs{{
            {AnalogInput::LeftY, ADC_CHANNEL_3},
            {AnalogInput::RightX, ADC_CHANNEL_4},
            {AnalogInput::RightY, ADC_CHANNEL_5},
            {AnalogInput::LeftTrigger, ADC_CHANNEL_6},
            {AnalogInput::RightTrigger, ADC_CHANNEL_7},
        }};
        for (const auto& [input, channel] : adc2_inputs) {
            if (adc_oneshot_config_channel(adc2_, channel, &channel_config) == ESP_OK) {
                analog_valid_[index_of(input)] = true;
            }
        }
    }

    // Average the resting values. Leave both sticks and triggers untouched for
    // this short period after RESET.
    std::array<std::int32_t, kAnalogCount> totals{};
    std::array<int, kAnalogCount> minimum{};
    std::array<int, kAnalogCount> maximum{};
    minimum.fill(kAdcMaximum);
    // A full second catches floating inputs that can look stable for only a
    // few milliseconds. Keep every raw channel available to RMH_ADC logging,
    // but only electrically stable channels are allowed into BLE reports.
    constexpr int kCalibrationSamples = 256;
    for (int sample = 0; sample < kCalibrationSamples; ++sample) {
        for (std::size_t index = 0; index < kAnalogCount; ++index) {
            if (!analog_valid_[index]) continue;
            const int raw = read_raw(static_cast<AnalogInput>(index));
            if (raw < 0) {
                analog_valid_[index] = false;
                continue;
            }
            totals[index] += raw;
            minimum[index] = std::min(minimum[index], raw);
            maximum[index] = std::max(maximum[index], raw);
        }
        vTaskDelay(pdMS_TO_TICKS(4));
    }
    for (std::size_t index = 0; index < kAnalogCount; ++index) {
        if (!analog_valid_[index]) continue;
        center_[index] = static_cast<int>(totals[index] / kCalibrationSamples);
        filtered_[index] = center_[index];
        trigger_peak_[index] = kInitialTriggerSpan;
        const int noise_span = maximum[index] - minimum[index];
        const bool stable = noise_span <= kCalibrationNoiseMaximum;
        const bool trigger = index >= index_of(AnalogInput::LeftTrigger);
        const bool sensible_rest = trigger
            ? (center_[index] <= kTriggerRestLowMaximum ||
               center_[index] >= kTriggerRestHighMinimum)
            : (center_[index] >= kStickCenterMinimum &&
               center_[index] <= kStickCenterMaximum);
        analog_valid_[index] = stable && sensible_rest;
        std::printf("RMH_ADC_STATUS input=%u center=%d span=%d BLE=%s\n",
                    static_cast<unsigned>(index), center_[index], noise_span,
                    analog_valid_[index] ? "ENABLED" : "BLOCKED");
        if (!analog_valid_[index]) {
            ESP_LOGW(kTag,
                     "Analog input %u blocked from BLE (center=%d span=%d); raw scan remains active",
                     static_cast<unsigned>(index), center_[index], noise_span);
        }
    }

    ready_ = true;
    ESP_LOGI(kTag,
             "Direct controls ready: Menu=GPIO43/TX Home=GPIO48; release controls at boot");
    std::printf("RMH_SCAN READY - press one physical control at a time\n");
    std::printf("RMH_SCAN DIGITAL lines show the real GPIO, not the guessed button name\n");
    std::printf("RMH_SCAN ORDER A B X Y LB RB LT RT L3 R3 UP DOWN LEFT RIGHT VIEW MENU HOME\n");
    std::fflush(stdout);
    return ESP_OK;
}

int DirectGamepad::read_raw(AnalogInput input) const {
    int raw = -1;
    switch (input) {
        case AnalogInput::LeftX:
            if (adc1_ != nullptr) (void)adc_oneshot_read(adc1_, ADC_CHANNEL_0, &raw);
            break;
        case AnalogInput::LeftY:
            if (adc2_ != nullptr) (void)adc_oneshot_read(adc2_, ADC_CHANNEL_3, &raw);
            break;
        case AnalogInput::RightX:
            if (adc2_ != nullptr) (void)adc_oneshot_read(adc2_, ADC_CHANNEL_4, &raw);
            break;
        case AnalogInput::RightY:
            if (adc2_ != nullptr) (void)adc_oneshot_read(adc2_, ADC_CHANNEL_5, &raw);
            break;
        case AnalogInput::LeftTrigger:
            if (adc2_ != nullptr) (void)adc_oneshot_read(adc2_, ADC_CHANNEL_6, &raw);
            break;
        case AnalogInput::RightTrigger:
            if (adc2_ != nullptr) (void)adc_oneshot_read(adc2_, ADC_CHANNEL_7, &raw);
            break;
        case AnalogInput::Count:
            break;
    }
    return raw;
}

std::int16_t DirectGamepad::read_stick(AnalogInput input, bool invert) {
    const auto index = index_of(input);
    if (!analog_valid_[index]) {
        latest_raw_[index] = -1;
        return 0;
    }
    const int raw = read_raw(input);
    latest_raw_[index] = raw;
    if (raw < 0) return 0;
    filtered_[index] = (filtered_[index] * 3 + raw) / 4;
    int delta = filtered_[index] - center_[index];
    if (std::abs(delta) <= kStickDeadZone) return 0;
    delta += delta < 0 ? kStickDeadZone : -kStickDeadZone;
    int value = delta * 32767 / (kStickSpan - kStickDeadZone);
    value = std::clamp(value, -32767, 32767);
    if (invert) value = -value;
    return static_cast<std::int16_t>(value);
}

std::uint16_t DirectGamepad::read_trigger(AnalogInput input) {
    const auto index = index_of(input);
    if (!analog_valid_[index]) {
        latest_raw_[index] = -1;
        return 0;
    }
    const int raw = read_raw(input);
    latest_raw_[index] = raw;
    if (raw < 0) return 0;
    filtered_[index] = (filtered_[index] * 3 + raw) / 4;
    int delta = std::abs(filtered_[index] - center_[index]);
    if (delta <= kTriggerNoiseFloor) return 0;
    delta -= kTriggerNoiseFloor;
    trigger_peak_[index] = std::max(trigger_peak_[index], delta);
    return static_cast<std::uint16_t>(
        std::clamp(delta * 1023 / trigger_peak_[index], 0, 1023));
}

void DirectGamepad::log_digital_changes() {
    bool wrote = false;
    for (std::size_t index = 0; index < kDigitalPins.size(); ++index) {
        const bool down = pressed(kDigitalPins[index]);
        if (digital_snapshot_ready_ && down != last_digital_[index]) {
            static std::uint32_t event_number = 0;
            ++event_number;
            std::printf("RMH_EVENT %lu GPIO%d %s\n",
                        static_cast<unsigned long>(event_number),
                        static_cast<int>(kDigitalPins[index]),
                        down ? "PRESSED" : "RELEASED");
            wrote = true;
        }
        last_digital_[index] = down;
    }
    digital_snapshot_ready_ = true;
    if (wrote) std::fflush(stdout);
}

void DirectGamepad::log_analog_changes() {
    // Called every 200 ms. Print a single compact line only if an ADC moved
    // enough to be useful for discovering stick/trigger wiring.
    bool changed = !analog_snapshot_ready_;
    for (std::size_t index = 0; index < kAnalogCount; ++index) {
        // Read even channels blocked from BLE so the terminal remains a useful
        // wiring probe without allowing floating voltages to move the gamepad.
        latest_raw_[index] = read_raw(static_cast<AnalogInput>(index));
        if (std::abs(latest_raw_[index] - last_logged_raw_[index]) >=
            kAnalogLogThreshold) {
            changed = true;
        }
    }
    if (!changed) return;
    std::printf("RMH_ADC GPIO1=%d GPIO14=%d GPIO15=%d GPIO16=%d GPIO17=%d GPIO18=%d\n",
                latest_raw_[0], latest_raw_[1], latest_raw_[2],
                latest_raw_[3], latest_raw_[4], latest_raw_[5]);
    last_logged_raw_ = latest_raw_;
    analog_snapshot_ready_ = true;
    std::fflush(stdout);
}

GamepadState DirectGamepad::read() {
    GamepadState state;
    if (!ready_) return state;
    log_digital_changes();
    for (const auto& binding : kButtons) {
        state.set(binding.button, pressed(binding.pin));
    }
    state.hat = read_hat();
    state.left_x = read_stick(AnalogInput::LeftX, false);
    state.left_y = read_stick(AnalogInput::LeftY, true);
    state.right_x = read_stick(AnalogInput::RightX, false);
    state.right_y = read_stick(AnalogInput::RightY, true);
    state.left_trigger = read_trigger(AnalogInput::LeftTrigger);
    state.right_trigger = read_trigger(AnalogInput::RightTrigger);
    if ((samples_ % 10U) == 0U) log_analog_changes();
    ++samples_;
    return state;
}

}  // namespace rmh
