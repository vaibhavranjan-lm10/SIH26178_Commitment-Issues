#!/usr/bin/env bash
# Builds the host-native MCU bridge simulator (TEST ONLY — see sim_main.c).
# Links the real mprpc.c/bridge_contract.c firmware sources; only sim_main.c
# and the TCP transport are test-only. Plain gcc, no Zephyr toolchain needed.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
MCU="$HERE/../.."
OUT="${1:-$HERE/sim_main}"
gcc -std=c11 -O1 -Wall -Wextra -I"$MCU/include" -o "$OUT" \
	"$MCU/src/mprpc.c" "$MCU/src/bridge_contract.c" "$HERE/sim_main.c"
echo "built $OUT"
