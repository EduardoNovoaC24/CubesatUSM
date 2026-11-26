#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
test_output_dir=$(mktemp -d)
trap 'rm -rf "$test_output_dir"' EXIT
cc -std=c11 -Wall -Wextra -Werror -ICore/Inc \
  tests/test_adsb.c Core/Src/adsb_protocol.c -lm -o "$test_output_dir/protocol"
"$test_output_dir/protocol"
for debug in 0 1; do
  cc -std=c11 -Wall -Wextra -Werror -DADSB_DEBUG_ENABLED=$debug -Itests/adsb_mock -ICore/Inc \
    tests/test_adsb_receiver.c Core/Src/adsb_receiver.c Core/Src/adsb_protocol.c \
    -o "$test_output_dir/receiver_$debug"
  "$test_output_dir/receiver_$debug"
done
