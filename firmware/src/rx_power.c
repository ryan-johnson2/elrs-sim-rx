#include "rx_power.h"
#include "rx_uart.h"
#include "board.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"

// ExpressLRS RX: power-on counter increments at boot, resets after 2 s uptime,
// bind mode entered when it reaches 3. Each ON must therefore be < 2000 ms but
// long enough for the RX to boot and write the counter.
#define CYCLE_ON_MS   1200
#define CYCLE_OFF_MS  400
#define CYCLE_COUNT   3

static bool     powered;
static uint32_t on_since_ms;

static bool     cycle_active;
static int      cycle_step;         // counts half-steps: on, off, on, off, ...
static uint32_t cycle_step_start_ms;

static void apply(bool on, uint32_t now_ms)
{
#if ELRS_LITE
    if (!on) return;                 // no switch: the receiver is on whenever USB is
#endif
    if (on) {
        gpio_put(PIN_RX_POWER, 1);
        rx_uart_enable(CRSF_BAUD);
        on_since_ms = now_ms;
    } else {
        rx_uart_disable();           // high-Z first so we never back-power
        gpio_put(PIN_RX_POWER, 0);
    }
    powered = on;
}

void rx_power_init(bool on)
{
    gpio_init(PIN_RX_POWER);
    gpio_put(PIN_RX_POWER, 0);
    gpio_set_dir(PIN_RX_POWER, GPIO_OUT);
    rx_uart_init();
    powered = false;
    cycle_active = false;
#if ELRS_LITE
    on = true;
#endif
    if (on) {
        apply(true, 0);
    }
}

void rx_power_set(bool on, uint32_t now_ms)
{
    cycle_active = false;
    if (on != powered) {
        apply(on, now_ms);
    }
}

bool rx_power_is_on(void)
{
    return powered;
}

uint32_t rx_power_on_since_ms(void)
{
    return on_since_ms;
}

void rx_power_start_bind_cycle(uint32_t now_ms)
{
#if ELRS_LITE
    (void)now_ms;                    // needs the power switch
    return;
#endif
    cycle_active = true;
    cycle_step = 0;
    cycle_step_start_ms = now_ms;
    apply(false, now_ms);
    // first half-step is an OFF gap so the first boot is a clean one
}

bool rx_power_bind_cycle_active(void)
{
    return cycle_active;
}

void rx_power_task(uint32_t now_ms)
{
    if (!cycle_active) return;

    // Sequence: off(gap) -> on -> off -> on -> off -> on (stay)
    // step 0: initial off gap
    // odd steps: on; even steps >= 2: off
    uint32_t dur = (cycle_step % 2 == 1) ? CYCLE_ON_MS : CYCLE_OFF_MS;
    if ((uint32_t)(now_ms - cycle_step_start_ms) < dur) return;

    cycle_step++;
    cycle_step_start_ms = now_ms;

    int ons_done = cycle_step / 2;          // completed on-periods so far
    if (cycle_step % 2 == 1) {
        apply(true, now_ms);
        if (ons_done + 1 >= CYCLE_COUNT) {
            // This is the final power-on; leave it on. RX will see its
            // third boot and enter bind mode.
            cycle_active = false;
        }
    } else {
        apply(false, now_ms);
    }
}
