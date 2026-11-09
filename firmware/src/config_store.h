// Persist config_t in the last flash sector.
#pragma once
#include <stdbool.h>
#include "mapping.h"

bool config_store_load(config_t *cfg);        // false if no valid config stored
bool config_store_save(const config_t *cfg);  // erases + programs one sector
