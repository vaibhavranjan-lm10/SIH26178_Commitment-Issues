/* RS-485 bus master transport: USART1 interrupt RX into a ring, byte parser
 * from Code A's protocol, DE held through the last stop bit on TX.
 * SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <hn/hw.h>

#define RS485_NODE DT_PATH(rs485)
static const struct device *const uart = DEVICE_DT_GET(DT_PHANDLE(RS485_NODE, uart));
static const struct gpio_dt_spec de = GPIO_DT_SPEC_GET(RS485_NODE, de_gpios);

#define RING 512
static struct {
	uint8_t buf[RING];
	volatile uint16_t head, tail;
} rx;
static struct rs485_parser parser;

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

int hw_bus_init(void)
{
	int rc;

	if (!device_is_ready(uart) || !gpio_is_ready_dt(&de)) {
		return -ENODEV;
	}
	rc = gpio_pin_configure_dt(&de, GPIO_OUTPUT_INACTIVE);
	if (rc) {
		return rc;
	}
	rs485_parser_init(&parser);
	rc = uart_irq_callback_user_data_set(uart, isr, NULL);
	if (rc) {
		return rc;
	}
	uart_irq_rx_enable(uart);
	return 0;
}

int hw_bus_send(void *ctx, const uint8_t *buf, size_t len)
{
	int spin = 5000;

	ARG_UNUSED(ctx);
	gpio_pin_set_dt(&de, 1);
	for (size_t i = 0; i < len; i++) {
		uart_poll_out(uart, buf[i]);
	}
	while (!uart_irq_tx_complete(uart) && spin-- > 0) {
		k_busy_wait(5);
	}
	gpio_pin_set_dt(&de, 0);
	return 0;
}

bool hw_bus_rx_frame(struct rs485_frame *out, uint32_t now_ms)
{
	while (rx.tail != rx.head) {
		uint8_t b = rx.buf[rx.tail];

		rx.tail = (uint16_t)((rx.tail + 1) % RING);
		if (rs485_parser_byte(&parser, b, now_ms)) {
			*out = parser.frame;
			return true;
		}
	}
	return false;
}

void hw_bus_idle(uint32_t now_ms)
{
	rs485_parser_idle(&parser, now_ms, 20);
}
