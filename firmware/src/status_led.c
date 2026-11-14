#include "status_led.h"
#include "board.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "ws2812.pio.h"

// Equal red, green and blue, so it looks the same whether the clone's LED is wired GRB or
// RGB. Kept dim: the LED is bright and sits bare on the module.
#define STATUS_LED_LEVEL 12

static PIO  pio = pio0;
static uint sm;
static bool ready;
static bool shown;

void status_led_init(void)
{
    // The same pattern goes to a plain LED, so one firmware serves the module and the
    // integrated board. On the RP2040-Zero this pin goes nowhere.
    gpio_init(PIN_STATUS_LED);
    gpio_set_dir(PIN_STATUS_LED, GPIO_OUT);
    gpio_put(PIN_STATUS_LED, 0);

    int claimed = pio_claim_unused_sm(pio, false);
    if (claimed < 0 || !pio_can_add_program(pio, &ws2812_program)) return;   // run without it
    sm = (uint)claimed;
    uint offset = pio_add_program(pio, &ws2812_program);

    pio_gpio_init(pio, PIN_WS2812);
    pio_sm_set_consecutive_pindirs(pio, sm, PIN_WS2812, 1, true);
    pio_sm_config c = ws2812_program_get_default_config(offset);
    sm_config_set_sideset_pins(&c, PIN_WS2812);
    sm_config_set_out_shift(&c, false, true, 24);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    float div = (float)clock_get_hz(clk_sys) / (800000.0f * (ws2812_T1 + ws2812_T2 + ws2812_T3));
    sm_config_set_clkdiv(&c, div);
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);

    ready = true;
    shown = true;               // force the first write
    status_led_set(false);
}

void status_led_set(bool on)
{
    gpio_put(PIN_STATUS_LED, on);
    if (!ready || on == shown) return;
    if (pio_sm_is_tx_fifo_full(pio, sm)) return;      // try again next loop, never block
    uint32_t v = on ? STATUS_LED_LEVEL : 0;
    pio_sm_put(pio, sm, ((v << 16) | (v << 8) | v) << 8u);
    shown = on;
}
