#!/usr/bin/env bash
# Build and run every Code B MCU ztest suite on native_sim (64-bit).
# Run from anywhere; uses the repo's single Zephyr workspace.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
WS="$HERE/../../../code_a_pod_firmware"
cd "$WS" || exit 1
export PATH="$WS/../.venv-zephyr/bin:$PATH"
BOARD=native_sim/native/64
fail=0
for t in tdma bus_master supervisor pps rails_alert_rpc; do
	echo "=== $t"
	if ! west build -p always -b "$BOARD" -d "build/hn_t_$t" "$HERE/$t" >"build/hn_t_$t.log" 2>&1; then
		echo "BUILD FAILED (see build/hn_t_$t.log)"; fail=1; continue
	fi
	out="$("./build/hn_t_$t/zephyr/zephyr.exe" 2>&1)"
	echo "$out" | grep -E "SUITE (PASS|FAIL)|FAIL -|Assert"
	if ! echo "$out" | grep -q "PROJECT EXECUTION SUCCESSFUL"; then
		echo "RUN FAILED"; fail=1
	fi
done
exit $fail
