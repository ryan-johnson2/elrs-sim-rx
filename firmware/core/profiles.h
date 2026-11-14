// Named mapping profiles. Hardware independent: this is the image kept in flash and the
// operations on it. One slot is active; the main loop works on a copy of it (app.config).
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "mapping.h"

#define PROFILE_COUNT     16
#define PROFILE_NAME_LEN  16            // including the terminating NUL
#define PROFILES_MAGIC    0x31465250u   // "PRF1"
#define PROFILES_VERSION  1

typedef struct {
    config_t cfg;
    char     name[PROFILE_NAME_LEN];    // empty = shown as "Profile N"
    uint8_t  used;
    uint8_t  reserved[3];
} profile_slot_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint8_t  active;                    // index of the slot loaded at boot, always a used one
    uint8_t  reserved[3];
    profile_slot_t slot[PROFILE_COUNT];
    uint32_t crc;                       // crc32 of everything above
} profile_store_t;

// Slot numbers here are 0-based; the console and page show them as 1..16.
void   profiles_init(profile_store_t *s, const config_t *first);     // slot 0 = *first, or defaults if NULL
void   profiles_finalize(profile_store_t *s);                        // sets magic/version/size/crc
bool   profiles_is_valid(const profile_store_t *s);
void   profiles_get(const profile_store_t *s, int n, config_t *out); // an empty slot gives the defaults
void   profiles_put(profile_store_t *s, int n, const config_t *cfg); // marks the slot used
bool   profiles_rename(profile_store_t *s, int n, const char *name); // false if nothing usable is left of the name
bool   profiles_clear(profile_store_t *s, int n);                    // false if n is the active slot
void   profiles_name(const profile_store_t *s, int n, char out[PROFILE_NAME_LEN]);
// Keeps letters, digits, spaces and - _ . + ( ) #, trims, cuts to 15 characters. Returns the length.
size_t profiles_clean_name(char out[PROFILE_NAME_LEN], const char *in);
