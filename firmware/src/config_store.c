#include "config_store.h"
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include <string.h>

// Last sector of flash.
#define CONFIG_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define CONFIG_FLASH_ADDR   (XIP_BASE + CONFIG_FLASH_OFFSET)
#define IMAGE_SIZE          ((sizeof(profile_store_t) + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE * FLASH_PAGE_SIZE)

_Static_assert(sizeof(profile_store_t) <= FLASH_SECTOR_SIZE, "profile store must fit one flash sector");

bool config_store_load(profile_store_t *s)
{
    const profile_store_t *stored = (const profile_store_t *)CONFIG_FLASH_ADDR;
    if (profiles_is_valid(stored)) {
        memcpy(s, stored, sizeof(*s));
        return true;
    }
    const config_t *legacy = (const config_t *)CONFIG_FLASH_ADDR;
    if (config_is_valid(legacy)) {
        profiles_init(s, legacy);
        return true;
    }
    return false;
}

bool config_store_save(const profile_store_t *s)
{
    if (!profiles_is_valid(s)) return false;

    // Skip the erase/program cycle if nothing changed.
    if (memcmp((const void *)CONFIG_FLASH_ADDR, s, sizeof(*s)) == 0) {
        return true;
    }

    static uint8_t image[IMAGE_SIZE] __attribute__((aligned(4)));
    memset(image, 0xFF, sizeof(image));
    memcpy(image, s, sizeof(*s));

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(CONFIG_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(CONFIG_FLASH_OFFSET, image, sizeof(image));
    restore_interrupts(ints);

    return memcmp((const void *)CONFIG_FLASH_ADDR, s, sizeof(*s)) == 0;
}
