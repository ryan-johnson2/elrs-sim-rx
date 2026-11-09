// Link-loss failsafe state machine. Hardware independent.
//
// Failsafe engages when no RC frame has arrived within timeout_ms, when the
// link quality reported after the last RC frame is zero, or when the receiver
// is unpowered. While engaged it asks the caller to send the failsafe report
// (every retry_ms) until the caller confirms a successful send. It re-arms
// when the link comes back.
#pragma once
#include <stdint.h>
#include <stdbool.h>

// ExpressLRS RX sends link statistics every 100 ms. An LQ-0 report older than
// this relative to the newest RC frame is stale (link came back since).
#define FAILSAFE_LQ_STALE_MS 150

typedef struct {
    uint32_t timeout_ms;
    uint32_t retry_ms;
    bool     active;
    bool     send_pending;
    uint32_t last_attempt_ms;
    uint32_t engage_count;
} failsafe_t;

typedef struct {
    uint32_t now_ms;
    bool     rx_powered;
    bool     have_rc;
    uint32_t last_rc_ms;
    bool     have_link;
    uint32_t last_link_ms;
    uint8_t  lq;
} failsafe_input_t;

void failsafe_init(failsafe_t *f, uint32_t timeout_ms, uint32_t retry_ms);

// Returns true when the caller should send the failsafe report now.
bool failsafe_update(failsafe_t *f, const failsafe_input_t *in);

// Tell the state machine whether the failsafe report went out.
void failsafe_report_sent(failsafe_t *f, bool ok);

bool failsafe_is_active(const failsafe_t *f);
