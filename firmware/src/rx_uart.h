// CRSF UART with interrupt-fed ring buffer, plus tri-state control so the
// lines cannot back-power an unpowered receiver.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

void   rx_uart_init(void);                 // sets pins to high-Z, UART off
void   rx_uart_enable(uint32_t baud);      // configure UART and pins
void   rx_uart_disable(void);              // UART off, pins high-Z
bool   rx_uart_is_enabled(void);
void   rx_uart_set_baud(uint32_t baud);
uint32_t rx_uart_get_baud(void);

size_t rx_uart_read(uint8_t *buf, size_t max);      // from ring buffer
// Same, also returning each byte's arrival time (ms since boot, stamped in the IRQ).
size_t rx_uart_read_ts(uint8_t *buf, uint32_t *ts_ms, size_t max);
void   rx_uart_discard(void);                        // drop everything buffered
void   rx_uart_write(const uint8_t *buf, size_t len); // blocking
size_t rx_uart_rx_overruns(void);
