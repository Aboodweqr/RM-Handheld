#include "usb_gamepad.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "usb/usb_host.h"

namespace rmh {
namespace {
constexpr char kTag[] = "rmh_usb";
}

UsbGamepad* UsbGamepad::active_ = nullptr;

esp_err_t UsbGamepad::start(StateCallback callback, void* context) {
    if (active_ != nullptr) return ESP_ERR_INVALID_STATE;
    state_callback_ = callback;
    callback_context_ = context;
    event_queue_ = xQueueCreate(6, sizeof(DeviceEvent));
    if (event_queue_ == nullptr) return ESP_ERR_NO_MEM;
    active_ = this;
    const auto created = xTaskCreatePinnedToCore(
        usb_task_entry, "rmh_usb", 6144, this, 6, nullptr, 0);
    return created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void UsbGamepad::usb_task_entry(void* context) {
    static_cast<UsbGamepad*>(context)->usb_task();
}

void UsbGamepad::usb_task() {
    // Give the TFT and BLE time to start and remain visible in the USB serial
    // monitor before switching the ESP32-S3's shared internal USB PHY to host.
    ESP_LOGI(kTag, "Starting USB host in 10 seconds");
    vTaskDelay(pdMS_TO_TICKS(10000));
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    const esp_err_t host_error = usb_host_install(&host_config);
    if (host_error != ESP_OK) {
        ESP_LOGE(kTag, "USB host unavailable: %s; TFT and BLE continue",
                 esp_err_to_name(host_error));
        vTaskDelete(nullptr);
        return;
    }

    const hid_host_driver_config_t driver_config = {
        .create_background_task = true,
        .task_priority = 5,
        .stack_size = 4096,
        .core_id = 0,
        .callback = driver_callback,
        .callback_arg = this,
    };
    const esp_err_t hid_error = hid_host_install(&driver_config);
    if (hid_error != ESP_OK) {
        ESP_LOGE(kTag, "USB HID unavailable: %s; TFT and BLE continue",
                 esp_err_to_name(hid_error));
        usb_host_uninstall();
        vTaskDelete(nullptr);
        return;
    }
    ESP_LOGI(kTag, "USB HID host ready");

    for (;;) {
        std::uint32_t event_flags = 0;
        const auto error = usb_host_lib_handle_events(pdMS_TO_TICKS(20), &event_flags);
        if (error != ESP_OK && error != ESP_ERR_TIMEOUT) {
            ESP_LOGW(kTag, "USB event error: %s", esp_err_to_name(error));
        }
        DeviceEvent event{};
        while (xQueueReceive(event_queue_, &event, 0) == pdTRUE) {
            if (event.event == HID_HOST_DRIVER_EVENT_CONNECTED) {
                open_device(event.handle);
            }
        }
    }
}

void UsbGamepad::driver_callback(hid_host_device_handle_t handle,
                                 hid_host_driver_event_t event, void* context) {
    auto* self = static_cast<UsbGamepad*>(context);
    const DeviceEvent message{handle, event};
    if (self != nullptr && self->event_queue_ != nullptr) {
        xQueueSend(self->event_queue_, &message, 0);
    }
}

void UsbGamepad::interface_callback(hid_host_device_handle_t handle,
                                    hid_host_interface_event_t event,
                                    void* context) {
    auto* self = static_cast<UsbGamepad*>(context);
    if (self != nullptr) self->handle_interface(handle, event);
}

void UsbGamepad::open_device(hid_host_device_handle_t handle) {
    const hid_host_device_config_t config = {
        .callback = interface_callback,
        .callback_arg = this,
    };
    auto error = hid_host_device_open(handle, &config);
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Cannot open HID interface: %s", esp_err_to_name(error));
        return;
    }

    hid_host_dev_info_t info{};
    if (hid_host_get_device_info(handle, &info) == ESP_OK) {
        vendor_id_.store(info.VID);
        product_id_.store(info.PID);
        ESP_LOGI(kTag, "USB_CONTROLLER VID=%04X PID=%04X",
                 static_cast<unsigned>(info.VID), static_cast<unsigned>(info.PID));
    }

    std::size_t descriptor_size = 0;
    const auto* descriptor = hid_host_get_report_descriptor(handle, &descriptor_size);
    if (descriptor != nullptr && descriptor_size != 0) {
        log_descriptor(descriptor, descriptor_size);
        if (!parser_.parse_descriptor(descriptor, descriptor_size)) {
            ESP_LOGW(kTag, "HID parser: %s; raw reports will still be logged",
                     parser_.error());
        } else {
            ESP_LOGI(kTag, "Mapped %u standard HID fields",
                     static_cast<unsigned>(parser_.field_count()));
        }
    } else {
        ESP_LOGW(kTag, "No HID report descriptor available");
    }

    error = hid_host_device_start(handle);
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Cannot start HID reports: %s", esp_err_to_name(error));
        hid_host_device_close(handle);
        return;
    }
    reports_received_.store(0);
    logged_changes_ = 0;
    last_report_size_ = 0;
    connected_.store(true);
}

void UsbGamepad::handle_interface(hid_host_device_handle_t handle,
                                  hid_host_interface_event_t event) {
    if (event == HID_HOST_INTERFACE_EVENT_INPUT_REPORT) {
        std::array<std::uint8_t, 64> bytes{};
        std::size_t length = 0;
        if (hid_host_device_get_raw_input_report_data(
                handle, bytes.data(), bytes.size(), &length) != ESP_OK) {
            return;
        }
        reports_received_.fetch_add(1);
        log_changed_report(bytes.data(), length);
        GamepadState state;
        if (parser_.decode_input(bytes.data(), length, state) && state_callback_ != nullptr) {
            state_callback_(state, callback_context_);
        }
    } else if (event == HID_HOST_INTERFACE_EVENT_DISCONNECTED) {
        connected_.store(false);
        vendor_id_.store(0);
        product_id_.store(0);
        parser_ = {};
        if (state_callback_ != nullptr) {
            const auto neutral = GamepadState::neutral();
            state_callback_(neutral, callback_context_);
        }
        hid_host_device_close(handle);
        ESP_LOGI(kTag, "USB controller disconnected");
    } else if (event == HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR) {
        ESP_LOGW(kTag, "USB controller transfer error");
    }
}

void UsbGamepad::log_descriptor(const std::uint8_t* bytes, std::size_t size) const {
    std::printf("USB_DESCRIPTOR VID=%04X PID=%04X DATA=",
                static_cast<unsigned>(vendor_id_.load()),
                static_cast<unsigned>(product_id_.load()));
    for (std::size_t index = 0; index < size; ++index) {
        std::printf("%02X", static_cast<unsigned>(bytes[index]));
    }
    std::printf("\n");
}

void UsbGamepad::log_changed_report(const std::uint8_t* bytes, std::size_t size) {
    const auto safe_size = std::min(size, last_report_.size());
    const bool changed = safe_size != last_report_size_ ||
                         std::memcmp(last_report_.data(), bytes, safe_size) != 0;
    if (!changed) return;
    std::copy(bytes, bytes + safe_size, last_report_.begin());
    last_report_size_ = safe_size;
    if (logged_changes_++ >= 200) return;
    std::printf("USB_REPORT N=%lu DATA=",
                static_cast<unsigned long>(reports_received_.load()));
    for (std::size_t index = 0; index < size; ++index) {
        std::printf("%02X", static_cast<unsigned>(bytes[index]));
    }
    std::printf("\n");
}

}  // namespace rmh
