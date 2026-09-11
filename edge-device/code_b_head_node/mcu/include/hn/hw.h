/* Hardware glue behind the pure modules.  One file per peripheral group.
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef HN_HW_H_
#define HN_HW_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <prahari/rs485.h>
#include <hn/pps.h>

/* Monotonic microseconds, software-extended from the cycle counter. */
uint32_t hw_now_us(void);
void hw_clock_service(void); /* call at least every few seconds */

/* RS-485 (USART1 + DE) */
int hw_bus_init(void);
int hw_bus_send(void *ctx, const uint8_t *buf, size_t len);
bool hw_bus_rx_frame(struct rs485_frame *out, uint32_t now_ms);
void hw_bus_idle(uint32_t now_ms);

/* SX1262 */
int hw_lora_init(uint8_t sf, uint32_t freq_hz, int8_t tx_dbm);
int hw_lora_send(const uint8_t *buf, size_t len);
int hw_lora_rx_start(void);
int hw_lora_rx_stop(void);
/* Pop one received packet; returns length or 0. */
int hw_lora_rx_pop(uint8_t *buf, size_t len, int16_t *rssi);

/* GNSS + PPS */
int hw_gnss_init(struct pps_disc *d);
bool hw_gnss_has_fix(void);

/* Watchdog */
int hw_wdt_init(uint32_t timeout_ms);
void hw_wdt_feed(void);

/* Rails + battery */
int hw_rails_init(void);
int hw_rail_set(void *ctx, uint8_t rail, bool on);
uint16_t hw_batt_mv(void);

/* Siren / relay */
int hw_alert_init(void);
void hw_alert_set(bool on);

/* MsgPack-RPC byte channel to the Linux side (bridge_contract.h), on the
 * internal MCU<->QRB2210 UART dedicated to it — binary, so it can no
 * longer share a UART with the text console (see boards/arduino_uno_q.overlay). */
int hw_rpc_init(void);
/* Copies out whatever bytes are ready (non-blocking); returns the count. */
size_t hw_rpc_read(uint8_t *buf, size_t max);
void hw_rpc_send_bytes(const uint8_t *buf, size_t len);

#endif /* HN_HW_H_ */
