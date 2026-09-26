#include <cstdint>

#include "ble_link.h"
#include "dashboard_ui.hpp"
#include "direct_gamepad.hpp"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "fuel_gauge.hpp"
#include "input_router.hpp"
#include "nvs_flash.h"
#include "telemetry_store.hpp"

namespace {

constexpr char kTag[] = "rm_handheld";
rmh::TelemetryStore g_telemetry;
SemaphoreHandle_t g_state_lock = nullptr;

std::uint32_t millis() {
    return static_cast<std::uint32_t>(esp_timer_get_time() / 1000ULL);
}

void receive_telemetry(const std::uint8_t* data, std::size_t length,
                       void* context) {
    (void)context;
    if (xSemaphoreTake(g_state_lock, pdMS_TO_TICKS(25)) != pdTRUE) return;
    const bool valid = g_telemetry.ingest(data, length, millis());
    xSemaphoreGive(g_state_lock);
    if (!valid) ESP_LOGW(kTag, "Rejected telemetry packet (length or CRC)");
}

constexpr std::uint16_t hid_button(std::uint8_t one_based_usage) {
    return static_cast<std::uint16_t>(1U << (one_based_usage - 1U));
}

std::uint16_t make_android_buttons(const rmh::GamepadState& state) {
    std::uint16_t buttons = 0;
    const auto map = [&](rmh::GamepadButton source,
                         std::uint8_t hid_usage) {
        if (state.pressed(source)) buttons |= hid_button(hid_usage);
    };

    // Android's generic HID gamepad map has C and Z placeholders at usages
    // 3 and 6. Sending our controls consecutively made X disappear, Y report
    // as X, LB report as Y, and Menu report as RB.
    map(rmh::GamepadButton::A, 1);
    map(rmh::GamepadButton::B, 2);
    map(rmh::GamepadButton::X, 4);
    map(rmh::GamepadButton::Y, 5);
    map(rmh::GamepadButton::LeftBumper, 7);
    map(rmh::GamepadButton::RightBumper, 8);
    map(rmh::GamepadButton::View, 11);
    map(rmh::GamepadButton::Menu, 12);
    map(rmh::GamepadButton::Home, 13);
    map(rmh::GamepadButton::LeftStick, 14);
    map(rmh::GamepadButton::RightStick, 15);

    // Android also expects digital L2/R2 buttons in slots 9/10 even when the
    // analog Brake/Accelerator axes are present.
    constexpr std::uint16_t kTriggerButtonThreshold = 96;
    if (state.left_trigger > kTriggerButtonThreshold) buttons |= hid_button(9);
    if (state.right_trigger > kTriggerButtonThreshold) buttons |= hid_button(10);
    return buttons;
}

rmh_ble_gamepad_report_t make_ble_report(const rmh::GamepadState& state) {
    return {
        .buttons = make_android_buttons(state),
        .hat = state.hat,
        .left_x = state.left_x,
        .left_y = state.left_y,
        .right_x = state.right_x,
        .right_y = state.right_y,
        .left_trigger = state.left_trigger,
        .right_trigger = state.right_trigger,
    };
}

}  // namespace

extern "C" void app_main(void) {
    esp_err_t error = nvs_flash_init();
    if (error == ESP_ERR_NVS_NO_FREE_PAGES ||
        error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        error = nvs_flash_init();
    }
    ESP_ERROR_CHECK(error);

    g_state_lock = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(g_state_lock == nullptr ? ESP_ERR_NO_MEM : ESP_OK);

    // Bring up the display first so hardware tests always have visible proof
    // that the application reached app_main, even while BLE is being debugged.
    // A disconnected/miswired TFT returns an error and does not block BLE.
    rmh::DashboardUi ui;
    error = ui.start();
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Display did not start: %s; BLE/controls will continue",
                 esp_err_to_name(error));
    } else {
        // Give LVGL time to send the first complete dashboard frame before the
        // radio controller is initialized.
        vTaskDelay(pdMS_TO_TICKS(250));
    }

    // Read the hard-wired controller before starting BLE. Sticks and triggers
    // are calibrated at rest during this short startup step.
    rmh::DirectGamepad gamepad;
    error = gamepad.begin();
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Direct controls did not start: %s",
                 esp_err_to_name(error));
    }

    ESP_LOGI(kTag, "Starting BLE");
    error = rmh_ble_start(receive_telemetry, nullptr);
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "BLE did not start: %s", esp_err_to_name(error));
    }

    rmh::FuelGauge fuel_gauge;
    fuel_gauge.begin();
    rmh::InputRouter router;
    std::uint32_t last_ui_update = 0;

    ESP_LOGI(kTag, "Ready. Open the RM Handheld APK and pair RM Handheld.");
    for (;;) {
        const auto now = millis();
        const rmh::GamepadState input = gamepad.read();

        const auto route = router.update(input, now);
        const auto ble_report = make_ble_report(route.forwarded);
        (void)rmh_ble_send_gamepad(&ble_report);

        if (ui.ready() && route.action != rmh::DashboardAction::None &&
            lvgl_port_lock(0)) {
            ui.show_page(route.page, route.selected_card, true);
            lvgl_port_unlock();
        }

        if (now - last_ui_update >= 500) {
            last_ui_update = now;
            rmh::LinkTelemetry link;
            link.controller_battery_percent = rmh::kUnknown8;  // X5 Lite is wired.
            link.handheld_battery_percent = fuel_gauge.available()
                                                    ? fuel_gauge.percent()
                                                    : rmh::kUnknown8;
            link.flags = (gamepad.ready() ? 1U : 0U) |
                         (rmh_ble_connected() ? 2U : 0U) |
                         (route.dashboard_active ? 4U : 0U) |
                         (fuel_gauge.available() ? 8U : 0U);
            link.usb_vendor_id = 0;
            link.usb_product_id = 0;
            link.reports_received = gamepad.reports_received();

            if (xSemaphoreTake(g_state_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
                g_telemetry.update_link(link, now);
                const bool phone_online = g_telemetry.phone_online(now);
                ui.refresh(g_telemetry, phone_online, rmh_ble_connected(),
                           gamepad.ready());
                xSemaphoreGive(g_state_lock);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
