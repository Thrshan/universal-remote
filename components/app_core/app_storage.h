#pragma once
#include <stddef.h>
#include "esp_err.h"

typedef struct {
    size_t remote_bytes;
    size_t free_bytes;
} app_storage_usage_t;
// Read-only NVS accounting; call on demand from the Storage page, never at boot.
esp_err_t app_storage_get_usage(app_storage_usage_t *usage);
