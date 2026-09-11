/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <string.h>
#include <hn/bus_master.h>

int bus_master_init(struct bus_master *b, const uint8_t *addrs, uint8_t n, uint16_t reply_timeout_ms,
		    uint16_t turnaround_ms, int (*send)(void *, const uint8_t *, size_t), void *ctx)
{
	if (!b || !send || n > BUS_MAX_PODS) {
		return -EINVAL;
	}
	memset(b, 0, sizeof(*b));
	for (uint8_t i = 0; i < n; i++) {
		if (!rs485_addr_valid(addrs[i])) {
			return -EINVAL;
		}
		b->pods[i].addr = addrs[i];
	}
	b->n_pods = n;
	b->reply_timeout_ms = reply_timeout_ms;
	b->turnaround_ms = turnaround_ms;
	b->send = send;
	b->ctx = ctx;
	return 0;
}

static int poll_pod(struct bus_master *b, uint8_t idx, uint32_t now)
{
	struct rs485_frame f = {.addr = b->pods[idx].addr, .cmd = RS485_CMD_POLL, .len = 1};
	uint8_t buf[RS485_FRAME_MAX];
	int n;

	f.payload[0] = b->seq++;
	n = rs485_frame_encode(&f, buf, sizeof(buf));
	if (n < 0) {
		return n;
	}
	b->pods[idx].polls++;
	b->state = BUS_WAIT_REPLY;
	b->t_state = now;
	return b->send(b->ctx, buf, (size_t)n);
}

int bus_sweep_start(struct bus_master *b, uint32_t now_ms)
{
	if (b->state != BUS_IDLE) {
		return -EBUSY;
	}
	if (b->n_pods == 0) {
		return -ENODEV;
	}
	b->idx = 0;
	return poll_pod(b, 0, now_ms);
}

bool bus_sweep_active(const struct bus_master *b)
{
	return b->state != BUS_IDLE;
}

/* Move to the next pod (or to the end-of-sweep sentinel) after a bus
 * turnaround gap.  SWEEP_DONE is emitted from bus_tick once the gap
 * after the last reply/timeout has elapsed. */
static void advance(struct bus_master *b, uint32_t now)
{
	b->idx++; /* may equal n_pods: nothing left to poll */
	b->state = BUS_TURNAROUND;
	b->t_state = now;
}

enum bus_event bus_rx_frame(struct bus_master *b, const struct rs485_frame *f, uint32_t now_ms,
			    uint8_t *pod_idx)
{
	struct bus_pod *p;

	if (b->state != BUS_WAIT_REPLY) {
		return BUS_EVT_NONE; /* late or unsolicited: ignored */
	}
	p = &b->pods[b->idx];
	if (f->addr != p->addr) {
		return BUS_EVT_NONE; /* a different pod (late reply): ignored */
	}
	if (f->cmd != RS485_CMD_REPORT) {
		p->bad_replies++;
		return BUS_EVT_NONE;
	}
	p->present = true;
	p->misses = 0;
	p->replies++;
	p->t_last_reply = now_ms;
	p->report_len = f->len;
	memcpy(p->report, f->payload, f->len);
	if (pod_idx) {
		*pod_idx = b->idx;
	}
	advance(b, now_ms);
	return BUS_EVT_REPORT;
}

enum bus_event bus_tick(struct bus_master *b, uint32_t now_ms, uint8_t *pod_idx)
{
	switch (b->state) {
	case BUS_WAIT_REPLY:
		if ((uint32_t)(now_ms - b->t_state) >= b->reply_timeout_ms) {
			struct bus_pod *p = &b->pods[b->idx];

			p->present = false;
			if (p->misses < 255) {
				p->misses++;
			}
			p->timeouts++;
			if (pod_idx) {
				*pod_idx = b->idx;
			}
			advance(b, now_ms);
			return BUS_EVT_MISSING;
		}
		return BUS_EVT_NONE;
	case BUS_TURNAROUND:
		if ((uint32_t)(now_ms - b->t_state) >= b->turnaround_ms) {
			if (b->idx >= b->n_pods) {
				b->state = BUS_IDLE;
				b->sweeps++;
				return BUS_EVT_SWEEP_DONE;
			}
			(void)poll_pod(b, b->idx, now_ms);
		}
		return BUS_EVT_NONE;
	default:
		return BUS_EVT_NONE;
	}
}

int bus_send_arm(struct bus_master *b, uint8_t addr, bool armed)
{
	struct rs485_frame f = {.addr = addr, .cmd = RS485_CMD_ARM, .len = 1};
	uint8_t buf[RS485_FRAME_MAX];
	int n;

	if (b->state != BUS_IDLE) {
		return -EBUSY;
	}
	f.payload[0] = armed ? 1 : 0;
	n = rs485_frame_encode(&f, buf, sizeof(buf));
	if (n < 0) {
		return n;
	}
	return b->send(b->ctx, buf, (size_t)n);
}

uint32_t bus_sweep_worst_case_ms(const struct bus_master *b)
{
	return (uint32_t)b->n_pods * ((uint32_t)b->reply_timeout_ms + b->turnaround_ms);
}
