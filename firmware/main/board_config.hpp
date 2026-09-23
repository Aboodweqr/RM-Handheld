#pragma once

#include "driver/gpio.h"
#include "driver/spi_master.h"

namespace rmh::board {

constexpr gpio_num_t kTftMosi = GPIO_NUM_11;
constexpr gpio_num_t kTftClock = GPIO_NUM_12;
constexpr gpio_num_t kTftChipSelect = GPIO_NUM_10;
constexpr gpio_num_t kTftDataCommand = GPIO_NUM_9;
constexpr gpio_num_t kTftReset = GPIO_NUM_8;
constexpr spi_host_device_t kTftSpiHost = SPI2_HOST;
// Start conservatively.  Long Dupont leads and inexpensive ST7789 breakout
// boards are often unreliable at 40 MHz and show coloured noise instead of a
// stable image.  10 MHz is still fast enough for this 320x240 dashboard.
constexpr int kTftPixelClockHz = 10 * 1000 * 1000;
constexpr int kDisplayWidth = 320;
constexpr int kDisplayHeight = 240;

// The ESP32-S3's native USB D-/D+ signals are fixed to GPIO19/GPIO20. On the
// pictured Super Mini they are routed to the board's USB-C connector.

}  // namespace rmh::board
