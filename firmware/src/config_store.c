#include "config_store.h"
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include <string.h>

// Last sector of flash.
#define CONFIG_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define CONFIG_FLASH_ADDR   (XIP_BASE + CONFIG_FLASH_OFFSET)

_Static_assert(sizeof(config_t) <= FLASH_PAGE_SIZE, "config_t must fit one flash page");

bool config_store_load(config_t *cfg)
{
    const config_t *stored = (const config_t *)CONFIG_FLASH_ADDR;
    if (!config_is_valid(stored)) {
        return false;
    }
    memcpy(cfg, stored, sizeof(*cfg));
    return true;
}

bool config_store_save(const config_t *cfg)
{
    if (!config_is_valid(cfg)) return false;

    // Skip the erase/program cycle if nothing changed.
    if (memcmp((const void *)CONFIG_FLASH_ADDR, cfg, sizeof(*cfg)) == 0) {
        return true;
    }

    static uint8_t page[FLASH_PAGE_SIZE] __attribute__((aligned(4)));
    memset(page, 0xFF, sizeof(page));
    memcpy(page, cfg, sizeof(*cfg));

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(CONFIG_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(CONFIG_FLASH_OFFSET, page, FLASH_PAGE_SIZE);
    restore_interrupts(ints);

    return memcmp((const void *)CONFIG_FLASH_ADDR, cfg, sizeof(*cfg)) == 0;
}
