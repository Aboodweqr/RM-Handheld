#pragma once
#include <cstdint>

using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
inline const char* esp_err_to_name(esp_err_t) { return "test"; }
inline void test_log(const char*, const char*, ...) {}
#define ESP_LOGI(...) test_log(__VA_ARGS__)
#define ESP_LOGW(...) test_log(__VA_ARGS__)

using gpio_num_t = int;
constexpr gpio_num_t GPIO_NUM_0 = 0;
constexpr gpio_num_t GPIO_NUM_1 = 1;
constexpr gpio_num_t GPIO_NUM_2 = 2;
constexpr gpio_num_t GPIO_NUM_3 = 3;
constexpr gpio_num_t GPIO_NUM_4 = 4;
constexpr gpio_num_t GPIO_NUM_5 = 5;
constexpr gpio_num_t GPIO_NUM_6 = 6;
constexpr gpio_num_t GPIO_NUM_7 = 7;
constexpr gpio_num_t GPIO_NUM_8 = 8;
constexpr gpio_num_t GPIO_NUM_9 = 9;
constexpr gpio_num_t GPIO_NUM_10 = 10;
constexpr gpio_num_t GPIO_NUM_11 = 11;
constexpr gpio_num_t GPIO_NUM_12 = 12;
constexpr gpio_num_t GPIO_NUM_13 = 13;
constexpr gpio_num_t GPIO_NUM_14 = 14;
constexpr gpio_num_t GPIO_NUM_15 = 15;
constexpr gpio_num_t GPIO_NUM_16 = 16;
constexpr gpio_num_t GPIO_NUM_17 = 17;
constexpr gpio_num_t GPIO_NUM_18 = 18;
constexpr gpio_num_t GPIO_NUM_19 = 19;
constexpr gpio_num_t GPIO_NUM_20 = 20;
constexpr gpio_num_t GPIO_NUM_21 = 21;
constexpr gpio_num_t GPIO_NUM_22 = 22;
constexpr gpio_num_t GPIO_NUM_23 = 23;
constexpr gpio_num_t GPIO_NUM_24 = 24;
constexpr gpio_num_t GPIO_NUM_25 = 25;
constexpr gpio_num_t GPIO_NUM_26 = 26;
constexpr gpio_num_t GPIO_NUM_27 = 27;
constexpr gpio_num_t GPIO_NUM_28 = 28;
constexpr gpio_num_t GPIO_NUM_29 = 29;
constexpr gpio_num_t GPIO_NUM_30 = 30;
constexpr gpio_num_t GPIO_NUM_31 = 31;
constexpr gpio_num_t GPIO_NUM_32 = 32;
constexpr gpio_num_t GPIO_NUM_33 = 33;
constexpr gpio_num_t GPIO_NUM_34 = 34;
constexpr gpio_num_t GPIO_NUM_35 = 35;
constexpr gpio_num_t GPIO_NUM_36 = 36;
constexpr gpio_num_t GPIO_NUM_37 = 37;
constexpr gpio_num_t GPIO_NUM_38 = 38;
constexpr gpio_num_t GPIO_NUM_39 = 39;
constexpr gpio_num_t GPIO_NUM_40 = 40;
constexpr gpio_num_t GPIO_NUM_41 = 41;
constexpr gpio_num_t GPIO_NUM_42 = 42;
constexpr gpio_num_t GPIO_NUM_43 = 43;
constexpr gpio_num_t GPIO_NUM_44 = 44;
constexpr gpio_num_t GPIO_NUM_45 = 45;
constexpr gpio_num_t GPIO_NUM_46 = 46;
constexpr gpio_num_t GPIO_NUM_47 = 47;
constexpr gpio_num_t GPIO_NUM_48 = 48;
constexpr int GPIO_MODE_INPUT = 1;
constexpr int GPIO_PULLUP_ENABLE = 1;
constexpr int GPIO_PULLDOWN_DISABLE = 0;
constexpr int GPIO_INTR_DISABLE = 0;
struct gpio_config_t {
    std::uint64_t pin_bit_mask;
    int mode, pull_up_en, pull_down_en, intr_type;
};
esp_err_t gpio_config(const gpio_config_t*);
int gpio_get_level(gpio_num_t);
using spi_host_device_t = int;
constexpr spi_host_device_t SPI2_HOST = 2;

using adc_oneshot_clk_src_t = int;
using adc_channel_t = int;
constexpr adc_channel_t ADC_CHANNEL_0 = 0;
constexpr adc_channel_t ADC_CHANNEL_1 = 1;
constexpr adc_channel_t ADC_CHANNEL_2 = 2;
constexpr adc_channel_t ADC_CHANNEL_3 = 3;
constexpr adc_channel_t ADC_CHANNEL_4 = 4;
constexpr adc_channel_t ADC_CHANNEL_5 = 5;
constexpr adc_channel_t ADC_CHANNEL_6 = 6;
constexpr adc_channel_t ADC_CHANNEL_7 = 7;
constexpr int ADC_UNIT_1 = 1, ADC_UNIT_2 = 2;
constexpr int ULP_MODE = 0, ADC_ULP_MODE_DISABLE = ULP_MODE;
constexpr int ADC_ATTEN_DB_12 = 12, ADC_BITWIDTH_12 = 12;
struct TestAdc { int unit; bool configured[10]{}; };
using adc_oneshot_unit_handle_t = TestAdc*;
struct adc_oneshot_unit_init_cfg_t { int unit_id, clk_src, ulp_mode; };
struct adc_oneshot_chan_cfg_t { int atten, bitwidth; };
esp_err_t adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t*, adc_oneshot_unit_handle_t*);
esp_err_t adc_oneshot_config_channel(adc_oneshot_unit_handle_t, adc_channel_t, const adc_oneshot_chan_cfg_t*);
esp_err_t adc_oneshot_read(adc_oneshot_unit_handle_t, adc_channel_t, int*);
inline int pdMS_TO_TICKS(int value) { return value; }
inline void vTaskDelay(int) {}
