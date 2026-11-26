#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
test_output_dir=$(mktemp -d)
trap 'rm -rf "$test_output_dir"' EXIT

cc -std=c11 -Wall -Wextra -Werror -ICore/Inc \
  tests/test_p2p.c Core/Src/p2p_protocol.c Core/Src/p2p_control.c \
  -lm -o "$test_output_dir/protocol"
"$test_output_dir/protocol"
cc -std=c11 -Wall -Wextra -Werror -Itests/p2p_flash_mock -ICore/Inc \
  tests/test_p2p_flash.c Core/Src/p2p_flash.c Core/Src/p2p_protocol.c Core/Src/p2p_control.c \
  -lm -o "$test_output_dir/flash"
"$test_output_dir/flash"
cc -std=c11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
  -Itests/radio_mock -ICore/Inc -IDrivers/SX126x \
  tests/test_radio_state.c Core/Src/radio_test.c Drivers/SX126x/sx126x.c \
  -Wl,--gc-sections -o "$test_output_dir/radio"
"$test_output_dir/radio"
cc -std=c11 -Wall -Wextra -Werror -ICore/Inc \
  -c Core/Src/p2p_protocol.c -o "$test_output_dir/protocol.o"
g++ -std=c++17 -Wall -Wextra -Werror -Itests/pico_mock \
  tests/test_pico_bridge.cpp "$test_output_dir/protocol.o" -o "$test_output_dir/pico"
"$test_output_dir/pico"
for mode in 0 1; do
  cc -std=c11 -Wall -Wextra -Werror -DSX1262_DIAGNOSTIC_MODE=$mode \
    -Itests/sx1262_mock -IDrivers/SX126x \
    tests/test_sx1262_hal.c Drivers/SX126x/sx126x_hal_stm32.c \
    -o "$test_output_dir/hal_$mode"
  "$test_output_dir/hal_$mode"
done
cmp Core/Inc/p2p_protocol.h pico_firmware/Pico_LoRa_P2P/p2p_protocol.h
cmp Core/Src/p2p_protocol.c pico_firmware/Pico_LoRa_P2P/p2p_protocol.c
