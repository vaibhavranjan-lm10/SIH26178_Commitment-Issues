# PRAHARI Code B — head node, MCU side (STM32U585 on the Arduino UNO Q).
# Blueprint §6.1: RS-485 bus master, LoRa TDMA timing, power sequencing,
# watchdog, GNSS pulse capture; plus the §6.8 siren/relay actuator.
#
# Build from the Zephyr workspace (code_a_pod_firmware/ holds the single
# Zephyr checkout for this repo):
#   cd code_a_pod_firmware && west build -b arduino_uno_q -d build/hn_mcu ../code_b_head_node/mcu
# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(prahari_head_node_mcu LANGUAGES C)

# The pod <-> head-node wire protocol is defined once, in Code A, and
# compiled into both sides.  This is a protocol dependency, not a
# dependency on pod behaviour.
set(PRAHARI_CODE_A ${CMAKE_CURRENT_SOURCE_DIR}/../../code_a_pod_firmware/app)

target_include_directories(app PRIVATE include ${PRAHARI_CODE_A}/include)

target_sources(app PRIVATE
  # hardware-independent (also compiled into the ztest suites)
  src/tdma.c
  src/pps.c
  src/bus_master.c
  src/supervisor.c
  src/rails.c
  src/alert.c
  src/mprpc.c
  src/bridge_contract.c
  ${PRAHARI_CODE_A}/src/wire.c
  ${PRAHARI_CODE_A}/src/rs485.c
  # hardware glue + main
  src/hw_bus.c
  src/hw_lora.c
  src/hw_gnss.c
  src/hw_wdt.c
  src/hw_rails.c
  src/hw_alert.c
  src/hw_rpc.c
  src/main.c
)
