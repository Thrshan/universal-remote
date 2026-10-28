#pragma once
#include "app_state.h"

// The state owner supplies the context. Mutations and rollback remain in app_state.c.
esp_err_t app_storage_save(app_ctx_t *app);
esp_err_t app_storage_load(app_ctx_t *app);
