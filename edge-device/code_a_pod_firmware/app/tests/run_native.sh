#!/usr/bin/env bash
# Build and run every Code A ztest suite on native_sim (64-bit host build;
# this host has no 32-bit multilib).  Run from the west workspace root:
#   ./app/tests/run_native.sh
set -u
cd "$(dirname "$0")/../.." || exit 1
export PATH="$PWD/../.venv-zephyr/bin:$PATH"
BOARD=native_sim/native/64
fail=0
for t in pod_position xdcr_select power sampler pipeline link; do
	echo "=== $t"
	if ! west build -p always -b "$BOARD" -d "build/t_$t" "app/tests/$t" >"build/t_$t.log" 2>&1; then
		echo "BUILD FAILED (see build/t_$t.log)"; fail=1; continue
	fi
	out="$("./build/t_$t/zephyr/zephyr.exe" 2>&1)"
	echo "$out" | grep -E "SUITE (PASS|FAIL)|FAIL -|Assert"
	if ! echo "$out" | grep -q "PROJECT EXECUTION SUCCESSFUL"; then
		echo "RUN FAILED"; fail=1
	fi
done
exit $fail
