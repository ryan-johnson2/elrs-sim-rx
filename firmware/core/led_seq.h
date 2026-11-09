// LED pattern sequencer reproducing the ExpressLRS receiver LED patterns.
// Hardware independent: call led_seq_update() with the clock and drive the
// LED with the returned state.
//
// Patterns are arrays of durations in 10 ms units, alternating on/off,
// starting with on, repeating forever. Values copied from ExpressLRS
// src/lib/LED/devLED.cpp.
#pragma once
#include <stdint.h>
#include <stdbool.h>

extern const uint8_t LEDSEQ_DISCONNECTED[2];   // 500 on, 500 off
extern const uint8_t LEDSEQ_BINDING[4];        // 100 on, 100 off, 100 on, 1000 off
extern const uint8_t LEDSEQ_WIFI_UPDATE[2];    // 20 on, 30 off
extern const uint8_t LEDSEQ_SERIAL_UPDATE[6];  // 200 on, 50 off, 50 on, 50 off, 50 on, 400 off
extern const uint8_t LEDSEQ_RADIO_FAILED[2];   // 200 on, 1000 off
extern const uint8_t LEDSEQ_MODEL_MISMATCH[6]; // 3 x 100 blink, 1000 off

typedef struct {
    const uint8_t *seq;     // NULL when solid/off
    uint8_t  len;
    uint8_t  idx;
    uint32_t step_start_ms;
    bool     solid_on;      // used when seq == NULL
} led_seq_t;

void led_seq_init(led_seq_t *l);

// Switch to a pattern. Re-applying the current pattern does not restart it.
void led_seq_set_pattern(led_seq_t *l, const uint8_t *seq, uint8_t len, uint32_t now_ms);
void led_seq_set_solid(led_seq_t *l, bool on);

// Returns desired LED state.
bool led_seq_update(led_seq_t *l, uint32_t now_ms);
