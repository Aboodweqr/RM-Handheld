#include <cstdint>

#include "ble_link.h"
#include "dashboard_ui.hpp"
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
#include "usb_gamepad.hpp"

namespace {

constexpr char kTag[] = "rm_handheld";
rmh::TelemetryStore g_telemetry;
rmh::GamepadState g_usb_state;
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

void receive_gamepad(const rmh::GamepadState& state, void* context) {
    (void)context;
    if (xSemaphoreTake(g_state_lock, pdMS_TO_TICKS(25)) != pdTRUE) return;
    g_usb_state = state;
    xSemaphoreGive(g_state_lock);
}

rmh_ble_gamepad_report_t make_ble_report(const rmh::GamepadState& state) {
    return {
        .buttons = static_cast<std::uint16_t>(state.buttons & 0xFFFFU),
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
        ESP_LOGE(kTag, "Display did not start: %s; BLE/USB will continue",
                 esp_err_to_name(error));
    } else {
        // Give LVGL time to send the first complete dashboard frame before the
        // radio controller is initialized.
        vTaskDelay(pdMS_TO_TICKS(250));
    }

    ESP_LOGI(kTag, "Starting BLE");
    error = rmh_ble_start(receive_telemetry, nullptr);
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "BLE did not start: %s", esp_err_to_name(error));
    }

    rmh::UsbGamepad usb_gamepad;
    error = usb_gamepad.start(receive_gamepad, nullptr);
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "USB host did not start: %s", esp_err_to_name(error));
    }

    rmh::FuelGauge fuel_gauge;
    fuel_gauge.begin();
    rmh::InputRouter router;
    std::uint32_t last_ui_update = 0;

    ESP_LOGI(kTag, "Ready. Open the RM Handheld APK and pair RM Handheld.");
    for (;;) {
        const auto now = millis();
        rmh::GamepadState input;
        if (xSemaphoreTake(g_state_lock, pdMS_TO_TICKS(10)) == pdTRUE) {
            input = g_usb_state;
            xSemaphoreGive(g_state_lock);
        }

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
            link.flags = (usb_gamepad.connected() ? 1U : 0U) |
                         (rmh_ble_connected() ? 2U : 0U) |
                         (route.dashboard_active ? 4U : 0U) |
                         (fuel_gauge.available() ? 8U : 0U);
            link.usb_vendor_id = usb_gamepad.vendor_id();
            link.usb_product_id = usb_gamepad.product_id();
            link.reports_received = usb_gamepad.reports_received();

            if (xSemaphoreTake(g_state_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
                g_telemetry.update_link(link, now);
                const bool phone_online = g_telemetry.phone_online(now);
                ui.refresh(g_telemetry, phone_online, rmh_ble_connected(),
                           usb_gamepad.connected());
                xSemaphoreGive(g_state_lock);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
