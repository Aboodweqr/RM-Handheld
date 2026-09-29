#!/usr/bin/env sh
set -eu

c++ -std=c++17 -Wall -Wextra -Werror -pedantic \
  -Iprotocol -Ifirmware/main \
  protocol/telemetry_protocol.cpp \
  firmware/main/hid_report_parser.cpp \
  firmware/main/input_router.cpp \
  firmware/main/telemetry_store.cpp \
  tests/host_tests.cpp \
  -o tests/host_tests

./tests/host_tests

# Compile the production GPIO reader against a small host HAL to verify
# digital triggers stay independent from ADC calibration and other controls.
c++ -std=c++20 -Wall -Wextra -Werror -pedantic \
  -Itests/esp_stubs -Ifirmware/main \
  firmware/main/direct_gamepad.cpp tests/direct_gamepad_tests.cpp \
  -o tests/direct_gamepad_tests

./tests/direct_gamepad_tests
