#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "gamepad_state.hpp"
#include "hid_report_parser.hpp"
#include "usb/hid_host.h"

namespace rmh {

class UsbGamepad {
public:
    using StateCallback = void (*)(const GamepadState& state, void* context);

    esp_err_t start(StateCallback callback, void* context);

    [[nodiscard]] bool connected() const { return connected_.load(); }
    [[nodiscard]] std::uint16_t vendor_id() const { return vendor_id_.load(); }
    [[nodiscard]] std::uint16_t product_id() const { return product_id_.load(); }
    [[nodiscard]] std::uint32_t reports_received() const {
        return reports_received_.load();
    }

private:
    struct DeviceEvent {
        hid_host_device_handle_t handle;
        hid_host_driver_event_t event;
    };

    static UsbGamepad* active_;
    static void usb_task_entry(void* context);
    static void driver_callback(hid_host_device_handle_t handle,
                                hid_host_driver_event_t event, void* context);
    static void interface_callback(hid_host_device_handle_t handle,
                                   hid_host_interface_event_t event,
                                   void* context);

    void usb_task();
    void open_device(hid_host_device_handle_t handle);
    void handle_interface(hid_host_device_handle_t handle,
                          hid_host_interface_event_t event);
    void log_descriptor(const std::uint8_t* bytes, std::size_t size) const;
    void log_changed_report(const std::uint8_t* bytes, std::size_t size);

    QueueHandle_t event_queue_{nullptr};
    StateCallback state_callback_{nullptr};
    void* callback_context_{nullptr};
    HidReportParser parser_{};
    std::atomic<bool> connected_{false};
    std::atomic<std::uint16_t> vendor_id_{0};
    std::atomic<std::uint16_t> product_id_{0};
    std::atomic<std::uint32_t> reports_received_{0};
    std::array<std::uint8_t, 64> last_report_{};
    std::size_t last_report_size_{0};
    std::uint16_t logged_changes_{0};
};

}  // namespace rmh
