#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "u8g2.h"

typedef enum {
    UI_KEY_UP,
    UI_KEY_DOWN,
    UI_KEY_OK,
    UI_KEY_BACK,
} ui_key_t;

typedef void (*ui_action_fn_t)(void *user_ctx);

typedef struct {
    const char    *label;
    uint16_t       glyph;     // Unicode symbol (works with unifont symbols)
    ui_action_fn_t action;    // called on OK
} ui_menu_item_t;

typedef struct {
    // U8g2 handle (provided by you)
    u8g2_t *u8;

    // Menu model
    const char *title;
    const ui_menu_item_t *items;
    uint8_t item_count;

    // UI state
    uint8_t selected;
    uint8_t top;              // first visible row
    bool    dirty;

    // optional context for actions
    void *user_ctx;
} ui_menu_t;

void ui_menu_init(ui_menu_t *m, u8g2_t *u8,
                  const char *title,
                  const ui_menu_item_t *items,
                  uint8_t item_count,
                  void *user_ctx);

void ui_menu_on_key(ui_menu_t *m, ui_key_t key);
void ui_menu_draw(ui_menu_t *m);   // call periodically or when dirty
