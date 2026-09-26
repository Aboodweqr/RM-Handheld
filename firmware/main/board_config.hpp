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

// Direct-wired GameSir X5 Lite controls. Every digital control is active-low:
// the switch joins its GPIO to GND while pressed. GPIO43 is the TX-labelled
// rear pad and is intentionally assigned to Menu; GPIO48 is Home.
constexpr gpio_num_t kButtonA = GPIO_NUM_21;
constexpr gpio_num_t kButtonB = GPIO_NUM_38;
constexpr gpio_num_t kButtonX = GPIO_NUM_40;
constexpr gpio_num_t kButtonY = GPIO_NUM_39;  // Correct physical Y reporting as X.
constexpr gpio_num_t kButtonLeftBumper = GPIO_NUM_41;
constexpr gpio_num_t kButtonRightBumper = GPIO_NUM_13;  // GPIO42 pad damaged.
constexpr gpio_num_t kButtonDpadUp = GPIO_NUM_47;
constexpr gpio_num_t kButtonDpadDown = GPIO_NUM_44;  // RX-labelled pad.
constexpr gpio_num_t kButtonDpadLeft = GPIO_NUM_2;
constexpr gpio_num_t kButtonDpadRight = GPIO_NUM_4;
constexpr gpio_num_t kButtonLeftStick = GPIO_NUM_5;
constexpr gpio_num_t kButtonRightStick = GPIO_NUM_6;
constexpr gpio_num_t kButtonView = GPIO_NUM_7;
constexpr gpio_num_t kButtonMenu = GPIO_NUM_43;      // TX-labelled pad.
constexpr gpio_num_t kButtonHome = GPIO_NUM_48;

// Analog Hall-sensor outputs. Never feed more than 3.3 V into these pins.
constexpr gpio_num_t kLeftStickX = GPIO_NUM_1;   // ADC1 channel 0.
constexpr gpio_num_t kLeftStickY = GPIO_NUM_14;  // ADC2 channel 3.
constexpr gpio_num_t kRightStickX = GPIO_NUM_15; // ADC2 channel 4.
constexpr gpio_num_t kRightStickY = GPIO_NUM_16; // ADC2 channel 5.
constexpr gpio_num_t kLeftTrigger = GPIO_NUM_17; // ADC2 channel 6.
constexpr gpio_num_t kRightTrigger = GPIO_NUM_18;// ADC2 channel 7.

}  // namespace rmh::board
