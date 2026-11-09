// Pin assignments for the RP2040-Zero prototype. See docs/HBOM.md pin plan.
#pragma once

#define PIN_CRSF_TX     0     // UART0 TX -> RX pad "RX"
#define PIN_CRSF_RX     1     // UART0 RX <- RX pad "TX"
#define PIN_RX_POWER    2     // high = receiver powered (via 2N7000 -> AO3401A)
#define PIN_BUTTON      3     // active low, internal pull-up
#define PIN_LED         4     // high = ring LED on (via 2N7000)
#define PIN_WS2812      16    // onboard RGB LED, debug only (unused for now)

#define CRSF_UART       uart0
#define CRSF_UART_IRQ   UART0_IRQ
#define CRSF_BAUD       420000

// Receiver boot grace: no frames expected this soon after power-up.
#define RX_BOOT_GRACE_MS        1500
// No frames for this long while powered = RX in WiFi mode or unresponsive.
#define RX_SILENT_MS            1000
// How long after a bind request to keep showing the binding pattern.
#define BIND_PATTERN_TIMEOUT_MS 60000
// Delay between powering the RX and sending the bind command.
#define BIND_AFTER_POWER_MS     1500

#define BUTTON_DEBOUNCE_MS      20
#define BUTTON_LONG_MS          1500

#define FAILSAFE_RETRY_MS       20
