/*
 * Switched-rail power controller (blueprint §3.4).
 *
 * Every transducer is fed from a switched 3.3 V rail through a load switch
 * under MCU control; nothing stays energised between reads except the
 * passive rain gauge (which has no domain at all).  The controller is a
 * pure state machine: time is passed in, switching goes through a backend,
 * so it runs unchanged under ztest.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef PRAHARI_POWER_H_
#define PRAHARI_POWER_H_

#include <stdbool.h>
#include <stdint.h>

/* Order MUST match the 'policy' enum in dts/bindings/prahari,load-switch.yaml. */
enum pwr_policy {
	PWR_POLICY_PER_READ = 0, /* on around each read, off after */
	PWR_POLICY_PERIODIC = 1, /* own duty cycle: on_ms every period_ms */
	PWR_POLICY_ARMED = 2,    /* on only while armed */
	PWR_POLICY_HOLD = 3,     /* stays on once acquired */
};

enum pwr_state {
	PWR_STATE_OFF = 0,
	PWR_STATE_SETTLING,
	PWR_STATE_ON,
};

enum pwr_event {
	PWR_EVT_READY,    /* rail on and settled: reads allowed */
	PWR_EVT_OFF,      /* rail switched off */
	PWR_EVT_TIMEOUT,  /* on_ms cap hit with the rail still held: forced off */
	PWR_EVT_DEFERRED, /* periodic window could not open (budget) */
};

struct pwr_domain_cfg {
	const char *name;
	enum pwr_policy policy;
	uint16_t current_ma; /* 0 = not specified, not budgeted */
	uint16_t settle_ms;
	uint32_t on_ms;      /* periodic: window length; others: cap, 0 = none */
	uint32_t period_ms;  /* periodic only */
};

struct pwr_domain_state {
	enum pwr_state state;
	uint8_t refs;
	bool armed;
	bool window_due;
	uint32_t t_on;
	uint32_t t_window;
};

struct pwr_backend {
	int (*set)(void *ctx, uint8_t dom, bool on);
	void *ctx;
};

typedef void (*pwr_event_cb_t)(void *ctx, uint8_t dom, enum pwr_event evt);

struct pwr_ctl {
	const struct pwr_domain_cfg *cfg;
	struct pwr_domain_state *st;
	uint8_t n;
	const struct pwr_backend *be;
	uint16_t budget_ma;
	pwr_event_cb_t cb;
	void *cb_ctx;
};

int pwr_init(struct pwr_ctl *c, const struct pwr_domain_cfg *cfg, struct pwr_domain_state *st,
	     uint8_t n, const struct pwr_backend *be, uint16_t budget_ma, uint32_t now_ms);
void pwr_set_event_cb(struct pwr_ctl *c, pwr_event_cb_t cb, void *ctx);

/*
 * acquire: take a reference for a read.
 *  per-read/hold : switches on if off.  -EBUSY if over budget.
 *  periodic      : -EAGAIN unless the window is open.
 *  armed         : -EACCES unless armed.
 * release: drop the reference; per-read switches off at zero refs.
 */
int pwr_acquire(struct pwr_ctl *c, uint8_t dom, uint32_t now_ms);
int pwr_release(struct pwr_ctl *c, uint8_t dom, uint32_t now_ms);
bool pwr_is_ready(const struct pwr_ctl *c, uint8_t dom, uint32_t now_ms);
uint32_t pwr_settle_remaining_ms(const struct pwr_ctl *c, uint8_t dom, uint32_t now_ms);

/* armed policy only: -ENOTSUP otherwise.  Disarming forces the rail off. */
int pwr_arm(struct pwr_ctl *c, uint8_t dom, bool armed, uint32_t now_ms);

/* Advance time: settle transitions, periodic windows, on-time caps.
 * pwr_tick covers every domain (main loop); pwr_tick_domain advances one
 * rail only, for a reader waiting on that rail's settle time. */
void pwr_tick(struct pwr_ctl *c, uint32_t now_ms);
void pwr_tick_domain(struct pwr_ctl *c, uint8_t dom, uint32_t now_ms);

uint32_t pwr_load_ma(const struct pwr_ctl *c);
bool pwr_all_off(const struct pwr_ctl *c);
enum pwr_state pwr_state(const struct pwr_ctl *c, uint8_t dom);

#endif /* PRAHARI_POWER_H_ */
