# PRAHARI Code A — pod firmware (L1).  One image for every pod position.
# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(prahari_pod LANGUAGES C)

target_include_directories(app PRIVATE include)

# Hardware-independent modules (also compiled into the ztest suites).
target_sources(app PRIVATE
  src/pod_position.c
  src/xdcr.c
  src/power.c
  src/pulse.c
  src/sampler.c
  src/calib.c
  src/median5.c
  src/zgate.c
  src/derive.c
  src/pipeline.c
  src/wire.c
  src/rs485.c
  src/lora_slot.c
  src/link_cfg.c
)

# Exactly one head-node link (blueprint §4.4).
target_sources_ifdef(CONFIG_PRAHARI_LINK_MODE_W app PRIVATE src/link_w.c)
target_sources_ifdef(CONFIG_PRAHARI_LINK_MODE_R app PRIVATE src/link_r.c)

# Devicetree-generated tables, class drivers and the boot/sampling loop.
target_sources(app PRIVATE
  src/pod_dt_tables.c
  src/drivers/xdcr_adc.c
  src/drivers/xdcr_pulse.c
  src/drivers/xdcr_sensor.c
  src/main.c
)
