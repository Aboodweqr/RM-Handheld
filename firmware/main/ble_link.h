#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*rmh_telemetry_receive_cb_t)(const uint8_t *data, size_t length,
                                           void *context);

typedef struct {
    uint16_t buttons;
    uint8_t hat;
    int16_t left_x;
    int16_t left_y;
    int16_t right_x;
    int16_t right_y;
    uint16_t left_trigger;
    uint16_t right_trigger;
} rmh_ble_gamepad_report_t;

esp_err_t rmh_ble_start(rmh_telemetry_receive_cb_t callback, void *context);
bool rmh_ble_connected(void);
esp_err_t rmh_ble_send_gamepad(const rmh_ble_gamepad_report_t *report);

#ifdef __cplusplus
}
#endif
