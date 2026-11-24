#include "rx_uart.h"
#include "board.h"
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/irq.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"

#define RING_SIZE 1024   // power of two

static uint8_t ring[RING_SIZE];
static uint32_t ring_ts[RING_SIZE];   // arrival time per byte, ms since boot
static volatile uint16_t ring_head;
static volatile uint16_t ring_tail;
static volatile size_t overruns;
static volatile uint32_t rx_bytes;            // diagnostics: total received since boot
static uint8_t recent[RX_UART_RECENT];        // and the last few, whatever the parser made of them
static bool enabled;
static uint32_t current_baud;

static void on_uart_rx(void)
{
    uint32_t now = to_ms_since_boot(get_absolute_time());
    uart_hw_t *hw = uart_get_hw(CRSF_UART);
    if (hw->rsr & UART_UARTRSR_OE_BITS) {   // hardware FIFO overrun
        overruns++;
        hw->rsr = 0;                        // write clears
    }
    while (uart_is_readable(CRSF_UART)) {
        uint8_t b = uart_getc(CRSF_UART);
        recent[rx_bytes % RX_UART_RECENT] = b;
        rx_bytes++;
        uint16_t next = (uint16_t)((ring_head + 1) & (RING_SIZE - 1));
        if (next != ring_tail) {
            ring[ring_head] = b;
            ring_ts[ring_head] = now;
            __dmb();
            ring_head = next;
        } else {
            overruns++;                     // software ring overrun
        }
    }
}

static void pins_high_z(void)
{
    gpio_init(PIN_CRSF_TX);
    gpio_init(PIN_CRSF_RX);
    gpio_set_dir(PIN_CRSF_TX, GPIO_IN);
    gpio_set_dir(PIN_CRSF_RX, GPIO_IN);
    gpio_disable_pulls(PIN_CRSF_TX);
    gpio_disable_pulls(PIN_CRSF_RX);
}

void rx_uart_init(void)
{
    ring_head = ring_tail = 0;
    overruns = 0;
    enabled = false;
    current_baud = CRSF_BAUD;
    pins_high_z();
}

void rx_uart_enable(uint32_t baud)
{
    if (enabled) {
        rx_uart_set_baud(baud);
        return;
    }
    ring_head = ring_tail = 0;
    current_baud = uart_init(CRSF_UART, baud);
    uart_set_format(CRSF_UART, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(CRSF_UART, true);
    gpio_set_function(PIN_CRSF_TX, GPIO_FUNC_UART);
    gpio_set_function(PIN_CRSF_RX, GPIO_FUNC_UART);
    gpio_pull_up(PIN_CRSF_RX);   // idle-high while the RX boots
    irq_set_exclusive_handler(CRSF_UART_IRQ, on_uart_rx);
    irq_set_enabled(CRSF_UART_IRQ, true);
    uart_set_irq_enables(CRSF_UART, true, false);
    enabled = true;
}

void rx_uart_disable(void)
{
    if (!enabled) return;
    uart_set_irq_enables(CRSF_UART, false, false);
    irq_set_enabled(CRSF_UART_IRQ, false);
    uart_deinit(CRSF_UART);
    pins_high_z();
    ring_head = ring_tail = 0;
    enabled = false;
}

bool rx_uart_is_enabled(void)
{
    return enabled;
}

void rx_uart_set_baud(uint32_t baud)
{
    if (!enabled || baud == 0) return;
    uart_tx_wait_blocking(CRSF_UART);
    current_baud = uart_set_baudrate(CRSF_UART, baud);
}

uint32_t rx_uart_get_baud(void)
{
    return current_baud;
}

size_t rx_uart_read_ts(uint8_t *buf, uint32_t *ts_ms, size_t max)
{
    size_t n = 0;
    while (n < max && ring_tail != ring_head) {
        __dmb();
        buf[n] = ring[ring_tail];
        if (ts_ms) ts_ms[n] = ring_ts[ring_tail];
        n++;
        ring_tail = (uint16_t)((ring_tail + 1) & (RING_SIZE - 1));
    }
    return n;
}

size_t rx_uart_read(uint8_t *buf, size_t max)
{
    return rx_uart_read_ts(buf, NULL, max);
}

void rx_uart_discard(void)
{
    ring_tail = ring_head;
}

void rx_uart_write(const uint8_t *buf, size_t len)
{
    if (!enabled) return;
    uart_write_blocking(CRSF_UART, buf, len);
}

size_t rx_uart_rx_overruns(void)
{
    return overruns;
}

uint32_t rx_uart_rx_bytes(void)
{
    return rx_bytes;
}

size_t rx_uart_recent(uint8_t *buf, size_t max)
{
    uint32_t save = save_and_disable_interrupts();
    uint32_t total = rx_bytes;
    size_t n = total < RX_UART_RECENT ? (size_t)total : RX_UART_RECENT;
    if (n > max) n = max;
    for (size_t i = 0; i < n; i++) buf[i] = recent[(total - n + i) % RX_UART_RECENT];
    restore_interrupts(save);
    return n;
}
