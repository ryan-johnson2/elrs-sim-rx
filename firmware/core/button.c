#include "button.h"

void button_init(button_t *b, uint32_t debounce_ms, uint32_t long_ms)
{
    b->debounce_ms = debounce_ms;
    b->long_ms = long_ms;
    b->raw_last = false;
    b->raw_change_ms = 0;
    b->stable = false;
    b->press_start_ms = 0;
    b->long_fired = false;
}

button_event_t button_update(button_t *b, bool pressed_raw, uint32_t now_ms)
{
    if (pressed_raw != b->raw_last) {
        b->raw_last = pressed_raw;
        b->raw_change_ms = now_ms;
    }

    button_event_t ev = BUTTON_EVENT_NONE;

    if (pressed_raw != b->stable &&
        (uint32_t)(now_ms - b->raw_change_ms) >= b->debounce_ms) {
        b->stable = pressed_raw;
        if (b->stable) {
            b->press_start_ms = now_ms;
            b->long_fired = false;
        } else if (!b->long_fired) {
            ev = BUTTON_EVENT_SHORT;
        }
    }

    if (b->stable && !b->long_fired &&
        (uint32_t)(now_ms - b->press_start_ms) >= b->long_ms) {
        b->long_fired = true;
        ev = BUTTON_EVENT_LONG;
    }

    return ev;
}

bool button_is_pressed(const button_t *b)
{
    return b->stable;
}
