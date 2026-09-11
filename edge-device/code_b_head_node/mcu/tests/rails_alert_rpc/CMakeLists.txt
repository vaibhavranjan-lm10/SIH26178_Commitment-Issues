# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(hn_test_rails_alert_rpc)
set(HN ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(PRAHARI_CODE_A ${HN}/../../code_a_pod_firmware/app)
target_include_directories(app PRIVATE ${HN}/include ${PRAHARI_CODE_A}/include)
target_sources(app PRIVATE src/main.c ${HN}/src/rails.c ${HN}/src/alert.c ${HN}/src/mprpc.c ${HN}/src/bridge_contract.c ${PRAHARI_CODE_A}/src/rs485.c ${PRAHARI_CODE_A}/src/wire.c)
