#include "dashboard_ui.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "board_config.hpp"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace rmh {
namespace {

constexpr char kTag[] = "rmh_display";
constexpr std::uint32_t kBackground = 0x070B0F;
constexpr std::uint32_t kPanel = 0x0D141A;
constexpr std::uint32_t kPanelBorder = 0x25353F;
constexpr std::uint32_t kAccent = 0x52E0B4;
constexpr std::uint32_t kWarm = 0xFFB454;
constexpr std::uint32_t kText = 0xF4F8FA;
constexpr std::uint32_t kMuted = 0x8E9EAB;
constexpr int kClearRows = 8;

// Keep both buffers unchanged while the SPI DMA queue is using them. A visible
// startup pattern distinguishes a working LCD link from an LVGL drawing issue.
DMA_ATTR std::uint16_t green_strip[board::kDisplayWidth * kClearRows];
DMA_ATTR std::uint16_t magenta_strip[board::kDisplayWidth * kClearRows];

esp_err_t show_panel_test(esp_lcd_panel_handle_t panel) {
    std::fill_n(green_strip, board::kDisplayWidth * kClearRows, 0x07E0);
    std::fill_n(magenta_strip, board::kDisplayWidth * kClearRows, 0xF81F);
    for (int y = 0; y < board::kDisplayHeight; y += kClearRows) {
        const int end_y = std::min(y + kClearRows, board::kDisplayHeight);
        const esp_err_t error = esp_lcd_panel_draw_bitmap(
            panel, 0, y, board::kDisplayWidth, end_y,
            y < board::kDisplayHeight / 2 ? green_strip : magenta_strip);
        if (error != ESP_OK) return error;
    }
    return ESP_OK;
}

esp_err_t display_error(const char* stage, esp_err_t error) {
    ESP_LOGE(kTag, "%s failed: %s", stage, esp_err_to_name(error));
    return error;
}

const char* page_name(DashboardPage page) {
    switch (page) {
        case DashboardPage::Overview: return "OVERVIEW";
        case DashboardPage::Battery: return "BATTERY + POWER";
        case DashboardPage::Temperature: return "TEMPERATURES";
        case DashboardPage::Performance: return "PERFORMANCE";
        case DashboardPage::Network: return "NETWORK";
        case DashboardPage::Controller: return "CONTROLLER";
        case DashboardPage::Animation: return "AMBIENT";
        default: return "RM HANDHELD";
    }
}

const char* thermal_name(std::uint8_t value) {
    switch (value) {
        case 0: return "NOMINAL";
        case 1: return "LIGHT";
        case 2: return "MODERATE";
        case 3: return "SEVERE";
        case 4: return "CRITICAL";
        case 5: return "EMERGENCY";
        case 6: return "SHUTDOWN";
        default: return "N/A";
    }
}

void plain(lv_obj_t* object) {
    lv_obj_remove_style_all(object);
}

void label_color(lv_obj_t* label, std::uint32_t color) {
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
}

void set_percent(lv_obj_t* label, std::uint8_t value, const char* suffix = "%") {
    if (value == kUnknown8) lv_label_set_text(label, "N/A");
    else lv_label_set_text_fmt(label, "%u%s", static_cast<unsigned>(value), suffix);
}

}  // namespace

esp_err_t DashboardUi::start() {
    const spi_bus_config_t bus_config = {
        .mosi_io_num = board::kTftMosi,
        .miso_io_num = GPIO_NUM_NC,
        .sclk_io_num = board::kTftClock,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .data4_io_num = GPIO_NUM_NC,
        .data5_io_num = GPIO_NUM_NC,
        .data6_io_num = GPIO_NUM_NC,
        .data7_io_num = GPIO_NUM_NC,
        .max_transfer_sz = board::kDisplayWidth * 24 * sizeof(std::uint16_t),
        .flags = 0,
        .isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO,
        .intr_flags = 0,
    };
    esp_err_t error = spi_bus_initialize(board::kTftSpiHost, &bus_config,
                                         SPI_DMA_CH_AUTO);
    if (error != ESP_OK) return display_error("SPI bus", error);

    const esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = board::kTftChipSelect,
        .dc_gpio_num = board::kTftDataCommand,
        .spi_mode = 0,
        .pclk_hz = board::kTftPixelClockHz,
        .trans_queue_depth = 10,
        .on_color_trans_done = nullptr,
        .user_ctx = nullptr,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .flags = {},
    };
    esp_lcd_panel_io_handle_t io = nullptr;
    error = esp_lcd_new_panel_io_spi(
        static_cast<esp_lcd_spi_bus_handle_t>(board::kTftSpiHost),
        &io_config, &io);
    if (error != ESP_OK) return display_error("SPI panel I/O", error);

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = board::kTftReset,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_BIG,
        .bits_per_pixel = 16,
        .flags = {},
        .vendor_config = nullptr,
    };
    esp_lcd_panel_handle_t panel = nullptr;
    error = esp_lcd_new_panel_st7789(io, &panel_config, &panel);
    if (error != ESP_OK) return display_error("ST7789 driver", error);
    error = esp_lcd_panel_reset(panel);
    if (error != ESP_OK) return display_error("LCD reset", error);
    error = esp_lcd_panel_init(panel);
    if (error != ESP_OK) return display_error("LCD init", error);
    error = esp_lcd_panel_invert_color(panel, true);
    if (error != ESP_OK) return display_error("LCD inversion", error);
    error = esp_lcd_panel_swap_xy(panel, true);
    if (error != ESP_OK) return display_error("LCD rotation", error);
    error = esp_lcd_panel_mirror(panel, false, true);
    if (error != ESP_OK) return display_error("LCD mirror", error);
    error = esp_lcd_panel_disp_on_off(panel, true);
    if (error != ESP_OK) return display_error("LCD on", error);
    error = show_panel_test(panel);
    if (error != ESP_OK) return display_error("LCD color test", error);
    ESP_LOGI(kTag, "Green/magenta LCD test submitted; look for both colors");
    vTaskDelay(pdMS_TO_TICKS(800));

    const lvgl_port_cfg_t lvgl_config = ESP_LVGL_PORT_INIT_CONFIG();
    error = lvgl_port_init(&lvgl_config);
    if (error != ESP_OK) return display_error("LVGL port", error);
    const lvgl_port_display_cfg_t display_config = {
        .io_handle = io,
        .panel_handle = panel,
        .control_handle = nullptr,
        .buffer_size = board::kDisplayWidth * 20,
        .double_buffer = true,
        .trans_size = 0,
        .hres = board::kDisplayWidth,
        .vres = board::kDisplayHeight,
        .monochrome = false,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .rounder_cb = nullptr,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma = true,
            .buff_spiram = false,
            .sw_rotate = false,
            .swap_bytes = false,
            .full_refresh = false,
            .direct_mode = false,
        },
    };
    display_ = lvgl_port_add_disp(&display_config);
    if (display_ == nullptr) return display_error("LVGL display", ESP_FAIL);

    if (!lvgl_port_lock(1000)) {
        return display_error("LVGL startup lock", ESP_ERR_TIMEOUT);
    }
    lv_display_set_default(display_);
    build_shell();
    show_page(DashboardPage::Overview, 0, false);
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(display_);
    lvgl_port_unlock();
    ESP_LOGI(kTag, "ST7789 dashboard started at 320x240 landscape");
    return ESP_OK;
}

void DashboardUi::build_shell() {
    auto* screen = lv_screen_active();
    plain(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(kBackground), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    auto* brand = lv_label_create(screen);
    lv_label_set_text(brand, "RM");
    lv_obj_set_style_text_font(brand, &lv_font_montserrat_18, LV_PART_MAIN);
    label_color(brand, kAccent);
    lv_obj_set_pos(brand, 8, 5);

    page_label_ = lv_label_create(screen);
    lv_label_set_text(page_label_, "OVERVIEW");
    lv_obj_set_style_text_font(page_label_, &lv_font_montserrat_12, LV_PART_MAIN);
    label_color(page_label_, kText);
    lv_obj_set_pos(page_label_, 42, 9);

    status_label_ = lv_label_create(screen);
    lv_label_set_text(status_label_, "PHONE --  USB --  BLE --");
    lv_obj_set_style_text_font(status_label_, &lv_font_montserrat_12, LV_PART_MAIN);
    label_color(status_label_, kMuted);
    lv_obj_align(status_label_, LV_ALIGN_TOP_RIGHT, -8, 9);

    content_ = lv_obj_create(screen);
    plain(content_);
    lv_obj_set_pos(content_, 0, 32);
    lv_obj_set_size(content_, board::kDisplayWidth, 208);
    lv_obj_clear_flag(content_, LV_OBJ_FLAG_SCROLLABLE);
}

void DashboardUi::show_page(DashboardPage page, std::uint8_t selected_card,
                            bool animate) {
    if (!ready()) return;
    const auto old_page = page_;
    page_ = page;
    selected_card_ = std::min<std::uint8_t>(selected_card, 5);
    slide_direction_ = static_cast<int>(page_) >= static_cast<int>(old_page) ? 1 : -1;
    lv_label_set_text(page_label_, page_name(page_));
    lv_obj_clean(content_);
    value_labels_.fill(nullptr);
    subtitle_labels_.fill(nullptr);
    detail_value_ = nullptr;
    detail_note_ = nullptr;
    chart_ = nullptr;
    chart_primary_ = nullptr;
    chart_secondary_ = nullptr;
    if (page_ == DashboardPage::Overview) build_overview();
    else build_detail();
    if (animate) apply_slide();
}

lv_obj_t* DashboardUi::create_card(int x, int y, const char* title,
                                   bool selected, std::size_t index) {
    auto* card = lv_obj_create(content_);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, 154, 62);
    lv_obj_set_style_bg_color(card, lv_color_hex(kPanel), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(card,
                                  lv_color_hex(selected ? kAccent : kPanelBorder),
                                  LV_PART_MAIN);
    lv_obj_set_style_border_width(card, selected ? 2 : 1, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 9, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 7, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    auto* title_label = lv_label_create(card);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_12, LV_PART_MAIN);
    label_color(title_label, kAccent);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, -1);

    value_labels_[index] = lv_label_create(card);
    lv_label_set_text(value_labels_[index], "--");
    lv_obj_set_style_text_font(value_labels_[index], &lv_font_montserrat_18,
                               LV_PART_MAIN);
    label_color(value_labels_[index], kText);
    lv_obj_align(value_labels_[index], LV_ALIGN_LEFT_MID, 0, 4);

    subtitle_labels_[index] = lv_label_create(card);
    lv_label_set_text(subtitle_labels_[index], "waiting");
    lv_obj_set_style_text_font(subtitle_labels_[index], &lv_font_montserrat_12,
                               LV_PART_MAIN);
    label_color(subtitle_labels_[index], kMuted);
    lv_obj_align(subtitle_labels_[index], LV_ALIGN_BOTTOM_LEFT, 0, 1);
    return card;
}

void DashboardUi::build_overview() {
    static const char* titles[] = {
        "PHONE BATTERY", "CHARGE POWER", "BATTERY TEMP",
        "THERMAL", "CPU / RAM", "GPU",
    };
    for (std::size_t index = 0; index < 6; ++index) {
        create_card(index % 2 == 0 ? 4 : 162,
                    static_cast<int>(index / 2) * 67 + 2,
                    titles[index], index == selected_card_, index);
    }
}

void DashboardUi::build_detail() {
    detail_value_ = lv_label_create(content_);
    lv_label_set_text(detail_value_, "--");
    lv_obj_set_style_text_font(detail_value_, &lv_font_montserrat_24, LV_PART_MAIN);
    label_color(detail_value_, kText);
    lv_obj_set_pos(detail_value_, 10, 7);

    detail_note_ = lv_label_create(content_);
    lv_label_set_text(detail_note_, "waiting for phone telemetry");
    lv_obj_set_style_text_font(detail_note_, &lv_font_montserrat_12, LV_PART_MAIN);
    label_color(detail_note_, kMuted);
    lv_obj_set_pos(detail_note_, 11, 38);

    if (page_ == DashboardPage::Controller) {
        auto* hint = lv_label_create(content_);
        lv_label_set_text(hint, "VIEW + MENU  2s : EXIT\nLB / RB : PAGE     B : OVERVIEW\n\nX5 LITE BATTERY: WIRED / NONE\nESP BATTERY: NO GAUGE");
        lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, LV_PART_MAIN);
        label_color(hint, kText);
        lv_obj_set_pos(hint, 12, 72);
        return;
    }
    if (page_ == DashboardPage::Animation) {
        auto* spinner = lv_spinner_create(content_);
        lv_obj_set_size(spinner, 104, 104);
        lv_obj_align(spinner, LV_ALIGN_CENTER, 0, 14);
        lv_spinner_set_anim_params(spinner, 900, 230);
        lv_obj_set_style_arc_color(spinner, lv_color_hex(kPanelBorder), LV_PART_MAIN);
        lv_obj_set_style_arc_color(spinner, lv_color_hex(kAccent), LV_PART_INDICATOR);
        lv_obj_set_style_arc_width(spinner, 8, LV_PART_MAIN);
        lv_obj_set_style_arc_width(spinner, 8, LV_PART_INDICATOR);
        return;
    }

    chart_ = lv_chart_create(content_);
    lv_obj_set_pos(chart_, 8, 64);
    lv_obj_set_size(chart_, 304, 132);
    lv_obj_set_style_bg_color(chart_, lv_color_hex(kPanel), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(chart_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(chart_, lv_color_hex(kPanelBorder), LV_PART_MAIN);
    lv_obj_set_style_border_width(chart_, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(chart_, 9, LV_PART_MAIN);
    lv_obj_set_style_line_color(chart_, lv_color_hex(kPanelBorder), LV_PART_MAIN);
    lv_chart_set_type(chart_, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart_, TelemetryStore::kHistoryPoints);
    lv_chart_set_div_line_count(chart_, 4, 6);
    lv_chart_set_range(chart_, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    chart_primary_ = lv_chart_add_series(chart_, lv_color_hex(kAccent),
                                         LV_CHART_AXIS_PRIMARY_Y);
    if (page_ == DashboardPage::Performance || page_ == DashboardPage::Battery ||
        page_ == DashboardPage::Temperature) {
        chart_secondary_ = lv_chart_add_series(chart_, lv_color_hex(kWarm),
                                               LV_CHART_AXIS_PRIMARY_Y);
    }
}

void DashboardUi::refresh(const TelemetryStore& telemetry, bool phone_online,
                          bool ble_connected, bool usb_connected) {
    if (!ready() || !lvgl_port_lock(250)) return;
    lv_label_set_text_fmt(status_label_, "PHONE %s  USB %s  BLE %s",
                          phone_online ? "OK" : "--",
                          usb_connected ? "OK" : "--",
                          ble_connected ? "OK" : "--");
    label_color(status_label_, phone_online ? kMuted : kWarm);
    if (page_ == DashboardPage::Overview) {
        update_overview(telemetry, phone_online, ble_connected, usb_connected);
    } else {
        update_detail(telemetry, phone_online, ble_connected, usb_connected);
    }
    lvgl_port_unlock();
}

void DashboardUi::update_overview(const TelemetryStore& telemetry,
                                  bool phone_online, bool ble_connected,
                                  bool usb_connected) {
    (void)ble_connected;
    const auto& core = telemetry.core();
    const auto& perf = telemetry.performance();
    set_percent(value_labels_[0], core.battery_percent);
    lv_label_set_text(subtitle_labels_[0],
                      (core.flags & kCharging) != 0 ? "CHARGING" :
                      (core.flags & kFull) != 0 ? "FULL" : "PHONE");
    if ((core.flags & kHasPower) != 0 && core.power_centi_w != kUnknown16) {
        lv_label_set_text_fmt(value_labels_[1], "%u.%02u W",
                              static_cast<unsigned>(core.power_centi_w / 100),
                              static_cast<unsigned>(core.power_centi_w % 100));
    } else lv_label_set_text(value_labels_[1], "N/A");
    if ((perf.flags & kHasChargerTemperature) != 0 &&
        perf.charger_temperature_deci_c != kUnknownSigned16) {
        const auto temp = perf.charger_temperature_deci_c;
        lv_label_set_text_fmt(subtitle_labels_[1], "CHARGER %d.%d C", temp / 10,
                              std::abs(temp % 10));
    } else if ((core.flags & kHasCurrent) != 0 && core.current_ma != kUnknownSigned16) {
        lv_label_set_text_fmt(subtitle_labels_[1], "%d mA EST",
                              std::abs(static_cast<int>(core.current_ma)));
    } else lv_label_set_text(subtitle_labels_[1], "CURRENT BLOCKED");
    if ((core.flags & kHasBatteryTemperature) != 0 &&
        core.battery_temperature_deci_c != kUnknownSigned16) {
        const auto temp = core.battery_temperature_deci_c;
        lv_label_set_text_fmt(value_labels_[2], "%d.%d C", temp / 10,
                              std::abs(temp % 10));
    } else lv_label_set_text(value_labels_[2], "N/A");
    if (core.cycle_count != kUnknown16) {
        lv_label_set_text_fmt(subtitle_labels_[2], "%u CYCLES",
                              static_cast<unsigned>(core.cycle_count));
    } else lv_label_set_text(subtitle_labels_[2], "CYCLES N/A");
    lv_label_set_text(value_labels_[3], thermal_name(core.thermal_status));
    if (core.thermal_headroom_percent != kUnknown8) {
        lv_label_set_text_fmt(subtitle_labels_[3], "%u%% HEADROOM",
                              static_cast<unsigned>(core.thermal_headroom_percent));
    } else lv_label_set_text(subtitle_labels_[3], "ANDROID STATUS");
    if ((perf.flags & kHasCpuUsage) != 0 &&
        perf.cpu_usage_percent != kUnknown8) {
        lv_label_set_text_fmt(value_labels_[4], "%u%% CPU",
                              static_cast<unsigned>(perf.cpu_usage_percent));
    } else lv_label_set_text(value_labels_[4], "CPU N/A");
    if (perf.cpu_mhz != kUnknown16 && perf.ram_usage_percent != kUnknown8) {
        lv_label_set_text_fmt(subtitle_labels_[4], "%u MHz / %u%% RAM",
                              static_cast<unsigned>(perf.cpu_mhz),
                              static_cast<unsigned>(perf.ram_usage_percent));
    } else if (perf.ram_usage_percent != kUnknown8) {
        lv_label_set_text_fmt(subtitle_labels_[4], "%u%% RAM",
                              static_cast<unsigned>(perf.ram_usage_percent));
    } else lv_label_set_text(subtitle_labels_[4], "RAM N/A");
    if ((perf.flags & kHasGpuUsage) != 0 &&
        perf.gpu_usage_percent != kUnknown8) {
        lv_label_set_text_fmt(value_labels_[5], "%u%% GPU",
                              static_cast<unsigned>(perf.gpu_usage_percent));
    } else lv_label_set_text(value_labels_[5], "GPU N/A");
    if (perf.gpu_mhz != kUnknown16) {
        lv_label_set_text_fmt(subtitle_labels_[5], "%u MHz",
                              static_cast<unsigned>(perf.gpu_mhz));
    } else lv_label_set_text(subtitle_labels_[5], "KERNEL BLOCKED");
    if (!phone_online) lv_label_set_text(subtitle_labels_[0], "OPEN APK");
    if (!usb_connected) lv_label_set_text(subtitle_labels_[5], "USB WAITING");
}

void DashboardUi::update_detail(const TelemetryStore& telemetry,
                                bool phone_online, bool ble_connected,
                                bool usb_connected) {
    const auto& core = telemetry.core();
    const auto& perf = telemetry.performance();
    const auto& network = telemetry.network();
    const auto& link = telemetry.link();
    const auto& identity = telemetry.identity();
    if (chart_ != nullptr && chart_primary_ != nullptr) {
        lv_chart_set_all_value(chart_, chart_primary_, LV_CHART_POINT_NONE);
        if (chart_secondary_ != nullptr) {
            lv_chart_set_all_value(chart_, chart_secondary_, LV_CHART_POINT_NONE);
        }
    }
    switch (page_) {
        case DashboardPage::Battery:
            if (core.battery_percent != kUnknown8) {
                if ((core.flags & kHasPower) != 0 &&
                    core.power_centi_w != kUnknown16) {
                    lv_label_set_text_fmt(detail_value_, "%u%%  |  %u.%02u W",
                                          static_cast<unsigned>(core.battery_percent),
                                          static_cast<unsigned>(core.power_centi_w / 100),
                                          static_cast<unsigned>(core.power_centi_w % 100));
                } else {
                    lv_label_set_text_fmt(detail_value_, "%u%%  |  POWER N/A",
                                          static_cast<unsigned>(core.battery_percent));
                }
            } else lv_label_set_text(detail_value_, "BATTERY N/A");
            if ((core.flags & kHasBatteryTemperature) != 0 &&
                core.battery_temperature_deci_c != kUnknownSigned16 &&
                (perf.flags & kHasChargerTemperature) != 0 &&
                perf.charger_temperature_deci_c != kUnknownSigned16) {
                lv_label_set_text_fmt(detail_note_, "BATTERY %d.%d C   CHARGER %d.%d C   60 SEC",
                    core.battery_temperature_deci_c / 10,
                    std::abs(core.battery_temperature_deci_c % 10),
                    perf.charger_temperature_deci_c / 10,
                    std::abs(perf.charger_temperature_deci_c % 10));
            } else if ((perf.flags & kHasChargerTemperature) != 0 &&
                       perf.charger_temperature_deci_c != kUnknownSigned16) {
                lv_label_set_text_fmt(detail_note_, "BATTERY N/A   CHARGER %d.%d C   60 SEC",
                    perf.charger_temperature_deci_c / 10,
                    std::abs(perf.charger_temperature_deci_c % 10));
            } else {
                lv_label_set_text(detail_note_, "GREEN: battery %   AMBER: power / 20 W");
            }
            for (std::size_t age = TelemetryStore::kHistoryPoints; age-- > 0;) {
                const auto battery = telemetry.battery_history(age);
                const auto power = telemetry.power_history(age);
                lv_chart_set_next_value(chart_, chart_primary_,
                                        battery == kUnknown8 ? LV_CHART_POINT_NONE : battery);
                lv_chart_set_next_value(chart_, chart_secondary_,
                    power == kUnknown16 ? LV_CHART_POINT_NONE : std::min<int>(100, power / 20));
            }
            break;
        case DashboardPage::Temperature: {
            const bool has_battery_temp =
                (core.flags & kHasBatteryTemperature) != 0 &&
                core.battery_temperature_deci_c != kUnknownSigned16;
            const bool has_charger_temp =
                (perf.flags & kHasChargerTemperature) != 0 &&
                perf.charger_temperature_deci_c != kUnknownSigned16;
            if (has_battery_temp) {
                lv_label_set_text_fmt(detail_value_, "BATTERY %d.%d C",
                    core.battery_temperature_deci_c / 10,
                    std::abs(core.battery_temperature_deci_c % 10));
            } else {
                lv_label_set_text(detail_value_, "BATTERY TEMP N/A");
            }
            if (has_charger_temp) {
                lv_label_set_text_fmt(detail_note_, "CHARGER %d.%d C   THERMAL %s   60 SEC",
                    perf.charger_temperature_deci_c / 10,
                    std::abs(perf.charger_temperature_deci_c % 10),
                    thermal_name(core.thermal_status));
            } else {
                lv_label_set_text_fmt(detail_note_, "CHARGER N/A   THERMAL %s   60 SEC",
                                      thermal_name(core.thermal_status));
            }
            for (std::size_t age = TelemetryStore::kHistoryPoints; age-- > 0;) {
                const auto battery_temp = telemetry.temperature_history(age);
                const auto charger_temp = telemetry.charger_temperature_history(age);
                lv_chart_set_next_value(chart_, chart_primary_,
                    battery_temp == kUnknownSigned16 ? LV_CHART_POINT_NONE
                                                     : std::clamp<int>(battery_temp / 10, 0, 100));
                lv_chart_set_next_value(chart_, chart_secondary_,
                    charger_temp == kUnknownSigned16 ? LV_CHART_POINT_NONE
                                                     : std::clamp<int>(charger_temp / 10, 0, 100));
            }
            break;
        }
        case DashboardPage::Performance: {
            char cpu_usage[8] = "N/A";
            char ram_usage[8] = "N/A";
            char cpu_clock[16] = "N/A";
            char gpu_clock[16] = "N/A";
            char cpu_temp[12] = "N/A";
            char gpu_temp[12] = "N/A";
            if ((perf.flags & kHasCpuUsage) != 0 &&
                perf.cpu_usage_percent != kUnknown8) {
                std::snprintf(cpu_usage, sizeof(cpu_usage), "%u%%",
                              static_cast<unsigned>(perf.cpu_usage_percent));
            }
            if (perf.ram_usage_percent != kUnknown8) {
                std::snprintf(ram_usage, sizeof(ram_usage), "%u%%",
                              static_cast<unsigned>(perf.ram_usage_percent));
            }
            if ((perf.flags & kHasCpuClock) != 0 && perf.cpu_mhz != kUnknown16) {
                std::snprintf(cpu_clock, sizeof(cpu_clock), "%uM",
                              static_cast<unsigned>(perf.cpu_mhz));
            }
            if ((perf.flags & kHasGpuClock) != 0 && perf.gpu_mhz != kUnknown16) {
                std::snprintf(gpu_clock, sizeof(gpu_clock), "%uM",
                              static_cast<unsigned>(perf.gpu_mhz));
            }
            if ((perf.flags & kHasCpuTemperature) != 0 &&
                perf.cpu_temperature_deci_c != kUnknownSigned16) {
                std::snprintf(cpu_temp, sizeof(cpu_temp), "%d.%dC",
                              perf.cpu_temperature_deci_c / 10,
                              std::abs(perf.cpu_temperature_deci_c % 10));
            }
            if ((perf.flags & kHasGpuTemperature) != 0 &&
                perf.gpu_temperature_deci_c != kUnknownSigned16) {
                std::snprintf(gpu_temp, sizeof(gpu_temp), "%d.%dC",
                              perf.gpu_temperature_deci_c / 10,
                              std::abs(perf.gpu_temperature_deci_c % 10));
            }
            lv_label_set_text_fmt(detail_value_, "CPU %s  |  RAM %s",
                                  cpu_usage, ram_usage);
            lv_label_set_text_fmt(detail_note_, "CPU %s / %s   GPU %s / %s",
                                  cpu_clock, cpu_temp, gpu_clock, gpu_temp);
            for (std::size_t age = TelemetryStore::kHistoryPoints; age-- > 0;) {
                const auto cpu = telemetry.cpu_history(age);
                const auto gpu = telemetry.gpu_history(age);
                lv_chart_set_next_value(chart_, chart_primary_,
                                        cpu == kUnknown8 ? LV_CHART_POINT_NONE : cpu);
                lv_chart_set_next_value(chart_, chart_secondary_,
                                        gpu == kUnknown8 ? LV_CHART_POINT_NONE : gpu);
            }
            break;
        }
        case DashboardPage::Network:
            if ((network.flags & 1U) != 0) {
                if ((identity.flags & 1U) != 0) {
                    lv_label_set_text_fmt(detail_value_, "WIFI %.13s", identity.wifi_name.data());
                } else {
                    lv_label_set_text(detail_value_, "WIFI CONNECTED");
                }
                char rssi[8] = "N/A";
                char receive[8] = "N/A";
                char transmit[8] = "N/A";
                char bluetooth[8] = "N/A";
                if (network.wifi_rssi_dbm != INT8_MIN) {
                    std::snprintf(rssi, sizeof(rssi), "%d", network.wifi_rssi_dbm);
                }
                if (network.receive_link_mbps != kUnknown16) {
                    std::snprintf(receive, sizeof(receive), "%u",
                                  static_cast<unsigned>(network.receive_link_mbps));
                }
                if (network.transmit_link_mbps != kUnknown16) {
                    std::snprintf(transmit, sizeof(transmit), "%u",
                                  static_cast<unsigned>(network.transmit_link_mbps));
                }
                if (network.bluetooth_connection_count != kUnknown8) {
                    std::snprintf(bluetooth, sizeof(bluetooth), "%u",
                                  static_cast<unsigned>(network.bluetooth_connection_count));
                }
                lv_label_set_text_fmt(detail_note_,
                                      "RSSI %s   RX %s   TX %s   BLE %s",
                                      rssi, receive, transmit, bluetooth);
            } else {
                lv_label_set_text(detail_value_, "WIFI N/A");
                lv_label_set_text(detail_note_, "Grant Nearby Wi-Fi in the APK");
            }
            for (std::size_t age = TelemetryStore::kHistoryPoints; age-- > 0;) {
                const auto rssi = telemetry.wifi_history(age);
                lv_chart_set_next_value(chart_, chart_primary_, rssi == INT8_MIN
                    ? LV_CHART_POINT_NONE
                    : std::clamp<int>(static_cast<int>(rssi) + 100, 0, 100));
            }
            break;
        case DashboardPage::Controller:
            lv_label_set_text_fmt(detail_value_, "USB %s  |  BLE %s",
                                  usb_connected ? "READY" : "WAIT",
                                  ble_connected ? "READY" : "WAIT");
            lv_label_set_text_fmt(detail_note_, "VID %04X  PID %04X  REPORTS %lu",
                                  static_cast<unsigned>(link.usb_vendor_id),
                                  static_cast<unsigned>(link.usb_product_id),
                                  static_cast<unsigned long>(link.reports_received));
            break;
        case DashboardPage::Animation:
            lv_label_set_text(detail_value_, phone_online ? "SYSTEM ONLINE" : "WAITING FOR APK");
            lv_label_set_text(detail_note_, "Ambient animation • LB / RB changes page");
            break;
        default: break;
    }
}

void DashboardUi::apply_slide() {
    lv_obj_set_x(content_, slide_direction_ * board::kDisplayWidth);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, content_);
    lv_anim_set_values(&animation, slide_direction_ * board::kDisplayWidth, 0);
    lv_anim_set_duration(&animation, 190);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&animation, [](void* object, std::int32_t value) {
        lv_obj_set_x(static_cast<lv_obj_t*>(object), value);
    });
    lv_anim_start(&animation);
}

}  // namespace rmh
