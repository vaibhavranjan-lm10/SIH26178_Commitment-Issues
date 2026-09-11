/*
 * Mode W — RS-485 half-duplex multi-drop, this pod as a polled slave
 * (blueprint §4.4, §5.2).  UART from the /rs485 node's 'uart' phandle,
 * driver-enable from its 'de-gpios'.  RX is interrupt-driven into a
 * small ring; parsing and the reply happen in link_service (thread
 * context).  The pod only ever transmits in direct answer to a unicast
 * frame addressed to it.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>
#include <prahari/link.h>
#include <prahari/rs485.h>
#include <prahari/wire.h>

#define RS485_NODE DT_PATH(rs485)
BUILD_ASSERT(DT_NODE_HAS_STATUS_OKAY(RS485_NODE),
	     "Mode W needs an enabled /rs485 node (prahari,rs485) in the board overlay");

static const struct device *const uart = DEVICE_DT_GET(DT_PHANDLE(RS485_NODE, uart));
static const struct gpio_dt_spec de = GPIO_DT_SPEC_GET(RS485_NODE, de_gpios);

#define RX_RING 64
static struct {
	uint8_t buf[RX_RING];
	volatile uint8_t head, tail;
} rx;

static K_SEM_DEFINE(rx_sem, 0, 1);
static struct rs485_parser parser;
static struct rs485_slave slave;
static uint8_t report_buf[WIRE_REPORT_MAX];
static uint8_t report_len;
static uint32_t polls, replies;

static void uart_isr(const struct device *dev, void *user_data)
{
	ARG_UNUSED(user_data);
	while (uart_irq_update(dev) && uart_irq_rx_ready(dev)) {
		uint8_t b;

		while (uart_fifo_read(dev, &b, 1) == 1) {
			uint8_t next = (uint8_t)((rx.head + 1) % RX_RING);

			if (next != rx.tail) {
				rx.buf[rx.head] = b;
				rx.head = next;
			} /* else: overrun, byte dropped; CRC will reject the frame */
		}
		k_sem_give(&rx_sem);
	}
}

static bool rx_pop(uint8_t *b)
{
	if (rx.tail == rx.head) {
		return false;
	}
	*b = rx.buf[rx.tail];
	rx.tail = (uint8_t)((rx.tail + 1) % RX_RING);
	return true;
}

int link_init(const struct link_identity *id, link_arm_cb_t arm_cb, void *ctx)
{
	int rc;

	if (!device_is_ready(uart) || !gpio_is_ready_dt(&de)) {
		return -ENODEV;
	}
	rc = gpio_pin_configure_dt(&de, GPIO_OUTPUT_INACTIVE); /* receive by default */
	if (rc) {
		return rc;
	}
	if (!rs485_addr_valid(id->addr)) {
		return -EINVAL;
	}
	slave.addr = id->addr;
	slave.position = id->position;
	slave.fw_major = PRAHARI_FW_MAJOR;
	slave.fw_minor = PRAHARI_FW_MINOR;
	slave.report = report_buf;
	slave.report_len = 0;
	slave.arm_cb = arm_cb;
	slave.ctx = ctx;
	rs485_parser_init(&parser);

	rc = uart_irq_callback_user_data_set(uart, uart_isr, NULL);
	if (rc) {
		return rc;
	}
	uart_irq_rx_enable(uart);
	return 0;
}

int link_publish(const struct pod_report *r)
{
	int n = wire_encode_report(r, slave.position, report_buf, sizeof(report_buf));

	if (n < 0) {
		return n;
	}
	report_len = (uint8_t)n;
	slave.report_len = report_len;
	return 0;
}

static void transmit(const uint8_t *buf, size_t n)
{
	int spin = 2000;

	gpio_pin_set_dt(&de, 1);
	for (size_t i = 0; i < n; i++) {
		uart_poll_out(uart, buf[i]);
	}
	/* Hold DE until the last byte has left the shift register. */
	while (!uart_irq_tx_complete(uart) && spin-- > 0) {
		k_busy_wait(10);
	}
	gpio_pin_set_dt(&de, 0);
}

void link_service(uint32_t now_ms)
{
	uint8_t b;

	rs485_parser_idle(&parser, now_ms, CONFIG_PRAHARI_RS485_IDLE_GAP_MS);
	while (rx_pop(&b)) {
		if (rs485_parser_byte(&parser, b, now_ms)) {
			struct rs485_frame out;
			uint8_t tx[RS485_FRAME_MAX];
			int rc = rs485_slave_handle(&slave, &parser.frame, &out);

			if (parser.frame.addr == slave.addr) {
				polls++;
			}
			if (rc == 1) {
				int n = rs485_frame_encode(&out, tx, sizeof(tx));

				if (n > 0) {
					transmit(tx, (size_t)n);
					replies++;
				}
			}
		}
	}
}

void link_wait(k_timeout_t timeout)
{
	(void)k_sem_take(&rx_sem, timeout);
}

const char *link_mode_name(void)
{
	return "W/RS-485";
}
