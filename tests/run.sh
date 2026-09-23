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
