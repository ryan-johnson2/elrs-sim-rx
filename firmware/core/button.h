// Debounced pushbutton with short/long press detection. Hardware independent.
//
// SHORT fires on release if the press was shorter than long_ms.
// LONG fires once, while still held, when the press reaches long_ms.
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    BUTTON_EVENT_NONE = 0,
    BUTTON_EVENT_SHORT,
    BUTTON_EVENT_LONG,
} button_event_t;

typedef struct {
    uint32_t debounce_ms;
    uint32_t long_ms;
    bool     raw_last;
    uint32_t raw_change_ms;
    bool     stable;
    uint32_t press_start_ms;
    bool     long_fired;
} button_t;

void button_init(button_t *b, uint32_t debounce_ms, uint32_t long_ms);

// pressed_raw is the current (active-high) physical state.
button_event_t button_update(button_t *b, bool pressed_raw, uint32_t now_ms);

bool button_is_pressed(const button_t *b);
