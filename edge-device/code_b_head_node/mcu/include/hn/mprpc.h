/*
 * mprpc — a minimal, spec-correct MessagePack encoder/decoder for the
 * MessagePack-RPC frames the UNO Q's native MPU<->MCU bridge carries.
 *
 * This is NOT a re-invented transport: MessagePack-RPC over a raw UART
 * byte stream (self-delimiting, no extra framing) is exactly what
 * Arduino_RPClite's SerialTransport + msgpack-rpc protocol *is*. This
 * module is a from-scratch, protocol-compatible reimplementation of the
 * wire format because the vendor's Arduino_RouterBridge C++ library
 * requires the Arduino core (HardwareSerial/Stream) that this freestanding
 * Zephyr app does not have — see code_b_head_node/README.md "Why a
 * reimplementation, not the vendored library" for the full justification.
 * It interops byte-for-byte with the official `msgpack` Python package
 * and the real Arduino_RouterBridge, because MessagePack is a fixed
 * public spec, not something either side gets to reinterpret.
 *
 * Only the subset the six bridge_contract.c message shapes need: nil,
 * bool, signed/unsigned int (fixint..int64/uint64), str (fixstr/str8/16),
 * bin (bin8/16), array (fixarray/array16), map (fixmap/map16). No float,
 * no ext types — bridge_contract.c never sends them, and the scanner
 * below reports an error rather than mis-parse if it ever sees one.
 *
 * Pure: no I/O, no Zephyr headers. Portable C11, compiled into the
 * Zephyr firmware image AND into the native (host gcc) test harness in
 * tests/native_bridge_sim/ — the exact same object code either way.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef HN_MPRPC_H_
#define HN_MPRPC_H_
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---------------------------------------------------------------- writer
 * Each mp_put_* appends to buf[*off .. cap), advancing *off. Returns 0 on
 * success, -1 if it would overflow cap (buffer left unmodified, *off
 * unchanged on overflow). Callers compose a full message by calling these
 * in sequence — see bridge_contract.c for the message shapes.
 */
int mp_put_nil(uint8_t *buf, size_t cap, size_t *off);
int mp_put_bool(uint8_t *buf, size_t cap, size_t *off, bool v);
int mp_put_int(uint8_t *buf, size_t cap, size_t *off, int64_t v);
int mp_put_str(uint8_t *buf, size_t cap, size_t *off, const char *s, size_t len);
int mp_put_bin(uint8_t *buf, size_t cap, size_t *off, const uint8_t *data, size_t len);
int mp_put_array_header(uint8_t *buf, size_t cap, size_t *off, size_t n);
int mp_put_map_header(uint8_t *buf, size_t cap, size_t *off, size_t n);

/* ---------------------------------------------------------------- reader
 * A cursor over a buffer known to hold at least one complete MessagePack
 * value (use mp_scan_value first to establish that). mp_get_* validates
 * the type tag itself and returns -1 (cursor left at the tag) if the next
 * value is not of the requested kind — callers branch on this, they never
 * need to peek the tag separately.
 */
struct mp_reader {
	const uint8_t *buf;
	size_t len;
	size_t pos;
};

void mp_reader_init(struct mp_reader *r, const uint8_t *buf, size_t len);
bool mp_get_nil(struct mp_reader *r);          /* consumes nil; false = not nil, cursor unmoved */
int mp_get_bool(struct mp_reader *r, bool *v);
int mp_get_int(struct mp_reader *r, int64_t *v);
/* *ptr aliases into r->buf; not copied, not nul-terminated. */
int mp_get_str(struct mp_reader *r, const char **ptr, size_t *len);
int mp_get_bin(struct mp_reader *r, const uint8_t **ptr, size_t *len);
int mp_get_array_header(struct mp_reader *r, size_t *n);
int mp_get_map_header(struct mp_reader *r, size_t *n);
/* Consume and discard one value of any type (arrays/maps recursively). */
int mp_skip_value(struct mp_reader *r);

/* ---------------------------------------------------------------- scanner
 * mp_scan_value walks exactly one top-level value starting at buf[0]
 * without materialising it, purely to find out how many bytes it takes —
 * this is the message-boundary detector a streaming byte source (UART,
 * TCP) needs, since MessagePack has no separate length prefix: the value
 * itself is the framing.
 */
enum mp_scan_result {
	MP_SCAN_OK = 0,        /* a complete value occupies buf[0 .. *msg_len) */
	MP_SCAN_NEED_MORE = 1, /* not enough bytes yet; caller should read more and retry */
	MP_SCAN_ERROR = -1,    /* not valid MessagePack (or an unsupported type) at buf[0] */
};
enum mp_scan_result mp_scan_value(const uint8_t *buf, size_t len, size_t *msg_len);

#endif /* HN_MPRPC_H_ */
