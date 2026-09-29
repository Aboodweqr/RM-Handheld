#include <array>
#include <cassert>
#include <cstdio>
#include "direct_gamepad.hpp"
#include "esp_test_hal.hpp"

namespace {
std::array<int, 49> levels;
std::uint64_t input_mask = 0;
TestAdc units[2]{{1, {}}, {2, {}}};
int adc_value = 2048;
}

esp_err_t gpio_config(const gpio_config_t* config) {
    assert(config->mode == GPIO_MODE_INPUT);
    assert(config->pull_up_en == GPIO_PULLUP_ENABLE);
    assert(config->pull_down_en == GPIO_PULLDOWN_DISABLE);
    input_mask = config->pin_bit_mask;
    return ESP_OK;
}
int gpio_get_level(gpio_num_t pin) {
    assert((input_mask & (1ULL << pin)) != 0);
    return levels.at(pin);
}
esp_err_t adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t* config,
                               adc_oneshot_unit_handle_t* handle) {
    *handle = &units[config->unit_id - 1];
    return ESP_OK;
}
esp_err_t adc_oneshot_config_channel(adc_oneshot_unit_handle_t unit,
                                    adc_channel_t channel,
                                    const adc_oneshot_chan_cfg_t*) {
    // ADC2 channels 6/7 are GPIO17/18: the trigger switches must never be
    // reconfigured as ADC inputs, which would lose their digital pull-ups.
    assert(unit->unit != 2 || (channel != 6 && channel != 7));
    unit->configured[channel] = true;
    return ESP_OK;
}
esp_err_t adc_oneshot_read(adc_oneshot_unit_handle_t unit,
                          adc_channel_t channel, int* value) {
    assert(unit->configured[channel]);
    *value = adc_value;
    return ESP_OK;
}

int main() {
    levels.fill(1);
    // A trigger held during boot must still work; only sticks calibrate.
    levels[17] = 0;
    rmh::DirectGamepad pad;
    assert(pad.begin() == ESP_OK);
    assert((input_mask & (1ULL << 17)) != 0);
    assert((input_mask & (1ULL << 18)) != 0);
    auto state = pad.read();
    assert(state.left_trigger == 1023 && state.right_trigger == 0);
    levels[17] = 1;
    state = pad.read();
    assert(state.left_trigger == 0 && state.right_trigger == 0);

    for (int pin : {17, 18}) {
        levels[pin] = 0;
        state = pad.read();
        assert(state.left_trigger == (pin == 17 ? 1023 : 0));
        assert(state.right_trigger == (pin == 18 ? 1023 : 0));
        assert(state.buttons == 0 && state.hat == 8);
        assert(state.left_x == 0 && state.left_y == 0);
        assert(state.right_x == 0 && state.right_y == 0);
        levels[pin] = 1;
        state = pad.read();
        assert(state.left_trigger == 0 && state.right_trigger == 0);
    }
    levels[17] = levels[18] = 0;
    state = pad.read();
    assert(state.left_trigger == 1023 && state.right_trigger == 1023);
    levels[17] = levels[18] = 1;

    // X/LB and each left D-pad contact must not become triggers or stick input.
    for (int pin : {39, 41, 47, 44, 2, 4}) {
        levels[pin] = 0;
        state = pad.read();
        assert(state.pressed(rmh::GamepadButton::X) == (pin == 39));
        assert(state.pressed(rmh::GamepadButton::LeftBumper) == (pin == 41));
        const int hat = pin == 47 ? 0 : pin == 44 ? 4 : pin == 2 ? 6 : pin == 4 ? 2 : 8;
        assert(state.hat == hat);
        assert(state.left_trigger == 0 && state.right_trigger == 0);
        assert(state.left_x == 0 && state.left_y == 0);
        assert(state.right_x == 0 && state.right_y == 0);
        levels[pin] = 1;
        state = pad.read();
        assert(state.buttons == 0 && state.hat == 8);
    }
    // Vary all analog channels through the range: trigger switches are independent.
    for (int raw : {0, 4095, 1600, 3300, 2048}) {
        adc_value = raw;
        for (int sample = 0; sample < 20; ++sample) {
            state = pad.read();
            assert(state.left_trigger == 0 && state.right_trigger == 0);
        }
    }
    std::puts("Direct GPIO/trigger regression tests passed.");
}
