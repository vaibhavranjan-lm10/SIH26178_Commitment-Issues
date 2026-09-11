/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>
#include <hn/mprpc.h>

/* MessagePack type tags actually used on this wire (spec, not invented). */
#define TAG_NIL 0xc0
#define TAG_FALSE 0xc2
#define TAG_TRUE 0xc3
#define TAG_BIN8 0xc4
#define TAG_BIN16 0xc5
#define TAG_UINT8 0xcc
#define TAG_UINT16 0xcd
#define TAG_UINT32 0xce
#define TAG_UINT64 0xcf
#define TAG_INT8 0xd0
#define TAG_INT16 0xd1
#define TAG_INT32 0xd2
#define TAG_INT64 0xd3
#define TAG_STR8 0xd9
#define TAG_STR16 0xda
#define TAG_ARRAY16 0xdc
#define TAG_MAP16 0xde
#define FIXINT_POS_MAX 0x7f
#define FIXINT_NEG_MIN (-32)
#define FIXSTR_TAG 0xa0
#define FIXSTR_MAX 31
#define FIXARR_TAG 0x90
#define FIXARR_MAX 15
#define FIXMAP_TAG 0x80
#define FIXMAP_MAX 15

/* ================================================================ writer */
static int put_u8(uint8_t *buf, size_t cap, size_t *off, uint8_t v)
{
	if (*off + 1 > cap) {
		return -1;
	}
	buf[(*off)++] = v;
	return 0;
}

static int put_bytes(uint8_t *buf, size_t cap, size_t *off, const uint8_t *data, size_t len)
{
	if (*off + len > cap) {
		return -1;
	}
	if (len) {
		memcpy(buf + *off, data, len);
	}
	*off += len;
	return 0;
}

static int put_be16(uint8_t *buf, size_t cap, size_t *off, uint16_t v)
{
	uint8_t b[2] = {(uint8_t)(v >> 8), (uint8_t)v};

	return put_bytes(buf, cap, off, b, 2);
}

static int put_be32(uint8_t *buf, size_t cap, size_t *off, uint32_t v)
{
	uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};

	return put_bytes(buf, cap, off, b, 4);
}

static int put_be64(uint8_t *buf, size_t cap, size_t *off, uint64_t v)
{
	for (int i = 0; i < 8; i++) {
		if (put_u8(buf, cap, off, (uint8_t)(v >> (56 - 8 * i)))) {
			return -1;
		}
	}
	return 0;
}

int mp_put_nil(uint8_t *buf, size_t cap, size_t *off)
{
	return put_u8(buf, cap, off, TAG_NIL);
}

int mp_put_bool(uint8_t *buf, size_t cap, size_t *off, bool v)
{
	return put_u8(buf, cap, off, v ? TAG_TRUE : TAG_FALSE);
}

int mp_put_int(uint8_t *buf, size_t cap, size_t *off, int64_t v)
{
	if (v >= 0 && v <= FIXINT_POS_MAX) {
		return put_u8(buf, cap, off, (uint8_t)v);
	}
	if (v < 0 && v >= FIXINT_NEG_MIN) {
		return put_u8(buf, cap, off, (uint8_t)(0xe0 | (v + 32)));
	}
	if (v >= INT32_MIN && v <= INT32_MAX) {
		return put_u8(buf, cap, off, TAG_INT32) || put_be32(buf, cap, off, (uint32_t)v);
	}
	return put_u8(buf, cap, off, TAG_INT64) || put_be64(buf, cap, off, (uint64_t)v);
}

int mp_put_str(uint8_t *buf, size_t cap, size_t *off, const char *s, size_t len)
{
	if (len <= FIXSTR_MAX) {
		return put_u8(buf, cap, off, (uint8_t)(FIXSTR_TAG | len)) ||
		       put_bytes(buf, cap, off, (const uint8_t *)s, len);
	}
	if (len <= UINT8_MAX) {
		return put_u8(buf, cap, off, TAG_STR8) || put_u8(buf, cap, off, (uint8_t)len) ||
		       put_bytes(buf, cap, off, (const uint8_t *)s, len);
	}
	if (len > UINT16_MAX) {
		return -1;
	}
	return put_u8(buf, cap, off, TAG_STR16) || put_be16(buf, cap, off, (uint16_t)len) ||
	       put_bytes(buf, cap, off, (const uint8_t *)s, len);
}

int mp_put_bin(uint8_t *buf, size_t cap, size_t *off, const uint8_t *data, size_t len)
{
	if (len <= UINT8_MAX) {
		return put_u8(buf, cap, off, TAG_BIN8) || put_u8(buf, cap, off, (uint8_t)len) ||
		       put_bytes(buf, cap, off, data, len);
	}
	if (len > UINT16_MAX) {
		return -1;
	}
	return put_u8(buf, cap, off, TAG_BIN16) || put_be16(buf, cap, off, (uint16_t)len) ||
	       put_bytes(buf, cap, off, data, len);
}

int mp_put_array_header(uint8_t *buf, size_t cap, size_t *off, size_t n)
{
	if (n <= FIXARR_MAX) {
		return put_u8(buf, cap, off, (uint8_t)(FIXARR_TAG | n));
	}
	if (n > UINT16_MAX) {
		return -1;
	}
	return put_u8(buf, cap, off, TAG_ARRAY16) || put_be16(buf, cap, off, (uint16_t)n);
}

int mp_put_map_header(uint8_t *buf, size_t cap, size_t *off, size_t n)
{
	if (n <= FIXMAP_MAX) {
		return put_u8(buf, cap, off, (uint8_t)(FIXMAP_TAG | n));
	}
	if (n > UINT16_MAX) {
		return -1;
	}
	return put_u8(buf, cap, off, TAG_MAP16) || put_be16(buf, cap, off, (uint16_t)n);
}

/* ================================================================ reader */
void mp_reader_init(struct mp_reader *r, const uint8_t *buf, size_t len)
{
	r->buf = buf;
	r->len = len;
	r->pos = 0;
}

static int peek_u8(const struct mp_reader *r, uint8_t *v)
{
	if (r->pos >= r->len) {
		return -1;
	}
	*v = r->buf[r->pos];
	return 0;
}

static uint64_t get_be(const struct mp_reader *r, size_t at, size_t n)
{
	uint64_t v = 0;

	for (size_t i = 0; i < n; i++) {
		v = (v << 8) | r->buf[at + i];
	}
	return v;
}

bool mp_get_nil(struct mp_reader *r)
{
	uint8_t t;

	if (peek_u8(r, &t) || t != TAG_NIL) {
		return false;
	}
	r->pos++;
	return true;
}

int mp_get_bool(struct mp_reader *r, bool *v)
{
	uint8_t t;

	if (peek_u8(r, &t) || (t != TAG_TRUE && t != TAG_FALSE)) {
		return -1;
	}
	*v = (t == TAG_TRUE);
	r->pos++;
	return 0;
}

int mp_get_int(struct mp_reader *r, int64_t *v)
{
	uint8_t t;
	size_t need;

	if (peek_u8(r, &t)) {
		return -1;
	}
	if (t <= FIXINT_POS_MAX) {
		*v = t;
		r->pos++;
		return 0;
	}
	if (t >= 0xe0) {
		*v = (int8_t)t;
		r->pos++;
		return 0;
	}
	switch (t) {
	case TAG_UINT8: need = 1; break;
	case TAG_UINT16: need = 2; break;
	case TAG_UINT32: need = 4; break;
	case TAG_UINT64: need = 8; break;
	case TAG_INT8: need = 1; break;
	case TAG_INT16: need = 2; break;
	case TAG_INT32: need = 4; break;
	case TAG_INT64: need = 8; break;
	default: return -1;
	}
	if (r->pos + 1 + need > r->len) {
		return -1;
	}
	uint64_t raw = get_be(r, r->pos + 1, need);

	switch (t) {
	case TAG_UINT8: case TAG_UINT16: case TAG_UINT32: case TAG_UINT64:
		*v = (int64_t)raw;
		break;
	case TAG_INT8: *v = (int8_t)raw; break;
	case TAG_INT16: *v = (int16_t)raw; break;
	case TAG_INT32: *v = (int32_t)raw; break;
	default: *v = (int64_t)raw; break; /* TAG_INT64 */
	}
	r->pos += 1 + need;
	return 0;
}

int mp_get_str(struct mp_reader *r, const char **ptr, size_t *len)
{
	uint8_t t;
	size_t hdr, n;

	if (peek_u8(r, &t)) {
		return -1;
	}
	if ((t & 0xe0) == FIXSTR_TAG) {
		hdr = 1;
		n = t & 0x1f;
	} else if (t == TAG_STR8) {
		if (r->pos + 2 > r->len) {
			return -1;
		}
		hdr = 2;
		n = r->buf[r->pos + 1];
	} else if (t == TAG_STR16) {
		if (r->pos + 3 > r->len) {
			return -1;
		}
		hdr = 3;
		n = (size_t)get_be(r, r->pos + 1, 2);
	} else {
		return -1;
	}
	if (r->pos + hdr + n > r->len) {
		return -1;
	}
	*ptr = (const char *)(r->buf + r->pos + hdr);
	*len = n;
	r->pos += hdr + n;
	return 0;
}

int mp_get_bin(struct mp_reader *r, const uint8_t **ptr, size_t *len)
{
	uint8_t t;
	size_t hdr, n;

	if (peek_u8(r, &t)) {
		return -1;
	}
	if (t == TAG_BIN8) {
		if (r->pos + 2 > r->len) {
			return -1;
		}
		hdr = 2;
		n = r->buf[r->pos + 1];
	} else if (t == TAG_BIN16) {
		if (r->pos + 3 > r->len) {
			return -1;
		}
		hdr = 3;
		n = (size_t)get_be(r, r->pos + 1, 2);
	} else {
		return -1;
	}
	if (r->pos + hdr + n > r->len) {
		return -1;
	}
	*ptr = r->buf + r->pos + hdr;
	*len = n;
	r->pos += hdr + n;
	return 0;
}

static int get_container_header(struct mp_reader *r, size_t *n, uint8_t fix_tag, uint8_t fix_max,
				uint8_t wide_tag)
{
	uint8_t t;

	if (peek_u8(r, &t)) {
		return -1;
	}
	if ((uint8_t)(t & ~fix_max) == fix_tag) {
		*n = t & fix_max;
		r->pos++;
		return 0;
	}
	if (t == wide_tag) {
		if (r->pos + 3 > r->len) {
			return -1;
		}
		*n = (size_t)get_be(r, r->pos + 1, 2);
		r->pos += 3;
		return 0;
	}
	return -1;
}

int mp_get_array_header(struct mp_reader *r, size_t *n)
{
	return get_container_header(r, n, FIXARR_TAG, FIXARR_MAX, TAG_ARRAY16);
}

int mp_get_map_header(struct mp_reader *r, size_t *n)
{
	return get_container_header(r, n, FIXMAP_TAG, FIXMAP_MAX, TAG_MAP16);
}

int mp_skip_value(struct mp_reader *r)
{
	uint8_t t;
	int64_t iv;
	bool bv;
	const char *sp;
	const uint8_t *bp;
	size_t n;

	if (peek_u8(r, &t)) {
		return -1;
	}
	if (mp_get_nil(r)) {
		return 0;
	}
	if (t == TAG_TRUE || t == TAG_FALSE) {
		return mp_get_bool(r, &bv);
	}
	if (t <= FIXINT_POS_MAX || t >= 0xe0 ||
	    (t >= TAG_UINT8 && t <= TAG_INT64 && t != 0xc1)) {
		return mp_get_int(r, &iv);
	}
	if ((t & 0xe0) == FIXSTR_TAG || t == TAG_STR8 || t == TAG_STR16) {
		return mp_get_str(r, &sp, &n);
	}
	if (t == TAG_BIN8 || t == TAG_BIN16) {
		return mp_get_bin(r, &bp, &n);
	}
	if ((t & 0xf0) == FIXARR_TAG || t == TAG_ARRAY16) {
		if (mp_get_array_header(r, &n)) {
			return -1;
		}
		for (size_t i = 0; i < n; i++) {
			if (mp_skip_value(r)) {
				return -1;
			}
		}
		return 0;
	}
	if ((t & 0xf0) == FIXMAP_TAG || t == TAG_MAP16) {
		if (mp_get_map_header(r, &n)) {
			return -1;
		}
		for (size_t i = 0; i < 2 * n; i++) {
			if (mp_skip_value(r)) {
				return -1;
			}
		}
		return 0;
	}
	return -1; /* float or other type this protocol never uses */
}

/* ================================================================ scanner
 * Re-runs the reader's own skip logic against a length hint, but must
 * distinguish "ran off the end of the supplied buffer" (need more bytes)
 * from "the bytes present are not valid MessagePack" (error). The reader
 * functions above already refuse to read past r->len, so a value that
 * scans successfully within a prefix necessarily fits; the only ambiguity
 * is a header that is itself truncated (e.g. a lone 0xd9 with no length
 * byte yet), which is exactly the case we can only resolve by asking for
 * more data. We treat every failure from the reader as "need more" up to
 * a generous cap, and only report a hard error for a lead byte that can
 * never be valid at all (float/ext, or a container/string bigger than
 * this protocol ever sends).
 */
#define MP_SCAN_MAX_LEN 4096

static bool tag_ever_valid(uint8_t t)
{
	if (t <= FIXINT_POS_MAX || t >= 0xe0) {
		return true;
	}
	if ((t & 0xe0) == FIXSTR_TAG || (t & 0xf0) == FIXARR_TAG || (t & 0xf0) == FIXMAP_TAG) {
		return true;
	}
	switch (t) {
	case TAG_NIL: case TAG_TRUE: case TAG_FALSE:
	case TAG_BIN8: case TAG_BIN16:
	case TAG_UINT8: case TAG_UINT16: case TAG_UINT32: case TAG_UINT64:
	case TAG_INT8: case TAG_INT16: case TAG_INT32: case TAG_INT64:
	case TAG_STR8: case TAG_STR16:
	case TAG_ARRAY16: case TAG_MAP16:
		return true;
	default:
		return false; /* float32/64, ext, str32/bin32/array32/map32: never sent on this wire */
	}
}

enum mp_scan_result mp_scan_value(const uint8_t *buf, size_t len, size_t *msg_len)
{
	if (len == 0) {
		return MP_SCAN_NEED_MORE;
	}
	if (!tag_ever_valid(buf[0])) {
		return MP_SCAN_ERROR;
	}
	if (len > MP_SCAN_MAX_LEN) {
		len = MP_SCAN_MAX_LEN; /* bound the scan; a real message is far smaller */
	}
	struct mp_reader r;

	mp_reader_init(&r, buf, len);
	if (mp_skip_value(&r) == 0) {
		*msg_len = r.pos;
		return MP_SCAN_OK;
	}
	/* Could not complete within the bytes on hand. A truly malformed lead
	 * tag was already rejected above, so this is a partial value. */
	return MP_SCAN_NEED_MORE;
}
