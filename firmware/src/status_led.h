// Onboard WS2812 on the RP2040-Zero, used as a plain on/off status light that mirrors the
// button ring. On the no-button build it is the only status light.
#pragma once
#include <stdbool.h>

void status_led_init(void);
void status_led_set(bool on);     // cheap to call every loop: only writes on a change
