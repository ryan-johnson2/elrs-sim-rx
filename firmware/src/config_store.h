// Persist the profile store in the last flash sector.
#pragma once
#include <stdbool.h>
#include "profiles.h"

// False if nothing usable is stored. A single config_t written by firmware from before
// profiles existed is accepted and becomes profile 1.
bool config_store_load(profile_store_t *s);
bool config_store_save(const profile_store_t *s);   // erases + programs one sector
