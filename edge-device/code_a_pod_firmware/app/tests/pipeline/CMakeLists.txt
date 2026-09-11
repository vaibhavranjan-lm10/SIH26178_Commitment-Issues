# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(prahari_test_pipeline)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
target_sources(app PRIVATE src/main.c
  ../../src/pipeline.c ../../src/calib.c ../../src/median5.c ../../src/zgate.c
  ../../src/derive.c ../../src/pod_position.c ../../src/xdcr.c)
