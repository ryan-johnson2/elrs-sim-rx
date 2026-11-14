// Pin assignments for the RP2040-Zero prototype. See docs/HBOM.md pin plan.
#pragma once

#define PIN_CRSF_TX     0     // UART0 TX -> RX pad "RX"
#define PIN_CRSF_RX     1     // UART0 RX <- RX pad "TX"
#define PIN_RX_POWER    2     // high = receiver powered (via 2N7000 -> AO3401A)
#define PIN_BUTTON      3     // active low, internal pull-up
#define PIN_LED         4     // high = ring LED on (via 2N7000)
#define PIN_WS2812      16    // RP2040-Zero's onboard RGB LED, mirrors the ring; the only status light on the lite build
#define PIN_STATUS_LED  25    // plain status LED on the integrated board (hardware/board); not connected on the RP2040-Zero

// Lite build: no receiver power switch, button or LED ring. Only GP0, GP1 and the onboard
// LED are used; the receiver is powered straight from USB 5 V.
#ifndef ELRS_LITE
#define ELRS_LITE 0
#endif
#define VARIANT_NAME    (ELRS_LITE ? "lite" : "full")

#define CRSF_UART       uart0
#define CRSF_UART_IRQ   UART0_IRQ
#define CRSF_BAUD       420000

// Receiver boot grace: a receiver that has sent nothing this long after power-up is
// treated as absent, in WiFi mode or unresponsive. Covers one pass of the 3.x rate scan
// (see RX_SILENT_MS), whose slowest step is about 7 s.
#define RX_BOOT_GRACE_MS        10000
// No frames for this long while powered = RX in WiFi mode or unresponsive.
// ExpressLRS 4.x sends link statistics every 100 ms while disconnected. 3.x (checked
// against 3.6.0 rx_main.cpp) sends only two, each time it steps to the next RF rate while
// scanning for a transmitter, and after a lost link it steps 5 to 10 times slower. At
// 150 Hz and up that leaves gaps of up to about 23 s, so anything much shorter than this
// makes the LED bounce between "disconnected" and "wifi" on a healthy 3.x receiver.
#define RX_SILENT_MS            30000
// How long after a bind request to keep showing the binding pattern.
#define BIND_PATTERN_TIMEOUT_MS 60000
// Delay between powering the RX and sending the bind command.
#define BIND_AFTER_POWER_MS     1500

#define BUTTON_DEBOUNCE_MS      20
#define BUTTON_LONG_MS          1500

#define FAILSAFE_RETRY_MS       20
