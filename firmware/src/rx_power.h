// Receiver power control: load switch GPIO, UART tri-state, and the
// power-cycle bind sequence (three boots under two seconds each).
#pragma once
#include <stdint.h>
#include <stdbool.h>

void     rx_power_init(bool on);
void     rx_power_set(bool on, uint32_t now_ms);
bool     rx_power_is_on(void);
uint32_t rx_power_on_since_ms(void);     // time of the last power-on

// Non-blocking power-cycle bind: 3 x (on, off) then on.
void     rx_power_start_bind_cycle(uint32_t now_ms);
bool     rx_power_bind_cycle_active(void);
void     rx_power_task(uint32_t now_ms);
