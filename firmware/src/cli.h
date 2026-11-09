// USB serial console: our text/JSON command set plus a Betaflight CLI
// emulation sufficient for the ExpressLRS flasher's passthrough handshake.
#pragma once
#include <stdint.h>
#include <stdbool.h>

void cli_init(void);
void cli_task(uint32_t now_ms);

// Called from TinyUSB when the host changes the serial port settings.
void cli_on_line_coding(uint32_t bit_rate);
