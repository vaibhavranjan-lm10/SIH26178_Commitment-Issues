/* MsgPack-RPC byte channel on the internal MCU<->QRB2210 UART (stand-in
 * for the Arduino bridge's SerialTransport; see hn/bridge_contract.h and
 * hn/mprpc.h). Binary, so unlike the earlier ad hoc line protocol this
 * UART can no longer double as the text console — see
 * boards/arduino_uno_q.overlay for the chosen-node change that follows
 * from that.
 * SPDX-License-Identifier: Apache-2.0 */
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <hn/hw.h>

#define RPC_NODE DT_CHOSEN(prahari_rpc_uart)
static const struct device *const uart = DEVICE_DT_GET(RPC_NODE);

#define RING 1024
static struct {
	uint8_t buf[RING];
	volatile uint16_t head, tail;
} rx;

static void isr(const struct device *dev, void *ud)
{
	ARG_UNUSED(ud);
	while (uart_irq_update(dev) && uart_irq_rx_ready(dev)) {
		uint8_t b;

		while (uart_fifo_read(dev, &b, 1) == 1) {
			uint16_t next = (uint16_t)((rx.head + 1) % RING);

			if (next != rx.tail) {
				rx.buf[rx.head] = b;
				rx.head = next;
			}
		}
	}
}

int hw_rpc_init(void)
{
	int rc;

	if (!device_is_ready(uart)) {
		return -ENODEV;
	}
	rc = uart_irq_callback_user_data_set(uart, isr, NULL);
	if (rc) {
		return rc;
	}
	uart_irq_rx_enable(uart);
	return 0;
}

size_t hw_rpc_read(uint8_t *buf, size_t max)
{
	size_t n = 0;

	while (n < max && rx.tail != rx.head) {
		buf[n++] = rx.buf[rx.tail];
		rx.tail = (uint16_t)((rx.tail + 1) % RING);
	}
	return n;
}

void hw_rpc_send_bytes(const uint8_t *buf, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		uart_poll_out(uart, buf[i]);
	}
}
