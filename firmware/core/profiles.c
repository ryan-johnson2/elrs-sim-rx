#include "profiles.h"
#include "crc.h"
#include <string.h>

static bool in_range(int n) { return n >= 0 && n < PROFILE_COUNT; }

void profiles_finalize(profile_store_t *s)
{
    s->magic   = PROFILES_MAGIC;
    s->version = PROFILES_VERSION;
    s->size    = (uint16_t)sizeof(profile_store_t);
    s->crc     = crc32_ieee((const uint8_t *)s, offsetof(profile_store_t, crc));
}

void profiles_init(profile_store_t *s, const config_t *first)
{
    memset(s, 0, sizeof(*s));
    if (first) {
        s->slot[0].cfg = *first;
        config_finalize(&s->slot[0].cfg);
    } else {
        config_defaults(&s->slot[0].cfg);
    }
    s->slot[0].used = 1;
    s->active = 0;
    profiles_finalize(s);
}

bool profiles_is_valid(const profile_store_t *s)
{
    if (s->magic != PROFILES_MAGIC) return false;
    if (s->version != PROFILES_VERSION) return false;
    if (s->size != sizeof(profile_store_t)) return false;
    if (s->crc != crc32_ieee((const uint8_t *)s, offsetof(profile_store_t, crc))) return false;
    if (s->active >= PROFILE_COUNT || !s->slot[s->active].used) return false;
    for (int n = 0; n < PROFILE_COUNT; n++) {
        if (!s->slot[n].used) continue;
        if (!config_is_valid(&s->slot[n].cfg)) return false;
        if (memchr(s->slot[n].name, 0, PROFILE_NAME_LEN) == NULL) return false;
    }
    return true;
}

void profiles_get(const profile_store_t *s, int n, config_t *out)
{
    if (in_range(n) && s->slot[n].used && config_is_valid(&s->slot[n].cfg)) {
        *out = s->slot[n].cfg;
    } else {
        config_defaults(out);
    }
}

void profiles_put(profile_store_t *s, int n, const config_t *cfg)
{
    if (!in_range(n)) return;
    s->slot[n].cfg = *cfg;
    config_finalize(&s->slot[n].cfg);
    s->slot[n].used = 1;
}

size_t profiles_clean_name(char out[PROFILE_NAME_LEN], const char *in)
{
    size_t n = 0;
    for (; *in && n < PROFILE_NAME_LEN - 1; in++) {
        char c = *in;
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == ' ' || c == '-' || c == '_' || c == '.' || c == '+' || c == '(' || c == ')' || c == '#';
        if (!ok) continue;
        if (c == ' ' && n == 0) continue;           // no leading spaces
        out[n++] = c;
    }
    while (n > 0 && out[n - 1] == ' ') n--;         // no trailing spaces
    out[n] = 0;
    return n;
}

bool profiles_rename(profile_store_t *s, int n, const char *name)
{
    char clean[PROFILE_NAME_LEN];
    if (!in_range(n) || !s->slot[n].used) return false;
    if (profiles_clean_name(clean, name) == 0) return false;
    memset(s->slot[n].name, 0, PROFILE_NAME_LEN);
    memcpy(s->slot[n].name, clean, strlen(clean));
    return true;
}

bool profiles_clear(profile_store_t *s, int n)
{
    if (!in_range(n) || n == s->active) return false;
    memset(&s->slot[n], 0, sizeof(s->slot[n]));
    return true;
}

void profiles_name(const profile_store_t *s, int n, char out[PROFILE_NAME_LEN])
{
    if (in_range(n) && s->slot[n].used && s->slot[n].name[0]) {
        memcpy(out, s->slot[n].name, PROFILE_NAME_LEN);
        out[PROFILE_NAME_LEN - 1] = 0;
    } else {
        // "Profile N", built by hand: N is at most two digits and the buffer is 16 bytes.
        int v = in_range(n) ? n + 1 : 0;
        size_t i = 8;
        memcpy(out, "Profile ", 8);
        if (v >= 10) out[i++] = (char)('0' + v / 10);
        out[i++] = (char)('0' + v % 10);
        out[i] = 0;
    }
}
