#include "led_seq.h"
#include <stddef.h>

const uint8_t LEDSEQ_DISCONNECTED[2]   = { 50, 50 };
const uint8_t LEDSEQ_BINDING[4]        = { 10, 10, 10, 100 };
const uint8_t LEDSEQ_WIFI_UPDATE[2]    = { 2, 3 };
const uint8_t LEDSEQ_SERIAL_UPDATE[6]  = { 20, 5, 5, 5, 5, 40 };
const uint8_t LEDSEQ_RADIO_FAILED[2]   = { 20, 100 };
const uint8_t LEDSEQ_MODEL_MISMATCH[6] = { 10, 10, 10, 10, 10, 100 };

void led_seq_init(led_seq_t *l)
{
    l->seq = NULL;
    l->len = 0;
    l->idx = 0;
    l->step_start_ms = 0;
    l->solid_on = false;
}

void led_seq_set_pattern(led_seq_t *l, const uint8_t *seq, uint8_t len, uint32_t now_ms)
{
    if (l->seq == seq) return;
    l->seq = seq;
    l->len = len;
    l->idx = 0;
    l->step_start_ms = now_ms;
}

void led_seq_set_solid(led_seq_t *l, bool on)
{
    l->seq = NULL;
    l->len = 0;
    l->solid_on = on;
}

bool led_seq_update(led_seq_t *l, uint32_t now_ms)
{
    if (l->seq == NULL || l->len == 0) {
        return l->solid_on;
    }
    // Advance through as many steps as the elapsed time covers.
    for (;;) {
        uint32_t step_ms = (uint32_t)l->seq[l->idx] * 10u;
        if ((uint32_t)(now_ms - l->step_start_ms) < step_ms) break;
        l->step_start_ms += step_ms;
        l->idx = (uint8_t)((l->idx + 1) % l->len);
    }
    return (l->idx % 2) == 0;   // even steps are "on"
}
