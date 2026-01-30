#include "ui_menu.h"
#include <string.h>

// How many rows fit on 128x64 with this layout
#define UI_ROWS_VISIBLE  4
#define UI_TITLE_H       12
#define UI_ROW_H         13

// Fonts:
// - unifont symbols gives you lots of unicode glyphs.
// - choose any u8g2 fonts you like; these are safe defaults.
static const uint8_t *FONT_TITLE  = u8g2_font_6x12_tf;
static const uint8_t *FONT_TEXT   = u8g2_font_6x12_tf;
static const uint8_t *FONT_SYMBOL = u8g2_font_unifont_t_symbols; // unicode symbols

static void clamp_scroll(ui_menu_t *m)
{
    if (m->selected < m->top) m->top = m->selected;

    uint8_t last_visible = m->top + (UI_ROWS_VISIBLE - 1);
    if (m->selected > last_visible) {
        m->top = m->selected - (UI_ROWS_VISIBLE - 1);
    }

    // prevent top from exceeding bounds
    if (m->item_count <= UI_ROWS_VISIBLE) {
        m->top = 0;
    } else {
        uint8_t max_top = m->item_count - UI_ROWS_VISIBLE;
        if (m->top > max_top) m->top = max_top;
    }
}

void ui_menu_init(ui_menu_t *m, u8g2_t *u8,
                  const char *title,
                  const ui_menu_item_t *items,
                  uint8_t item_count,
                  void *user_ctx)
{
    memset(m, 0, sizeof(*m));
    m->u = u8;
    m->title = title;
    m->items = items;
    m->item_count = item_count;
    m->selected = 0;
    m->top = 0;
    m->dirty = true;
    m->user_ctx = user_ctx;
}

void ui_menu_on_key(ui_menu_t *m, ui_key_t key)
{
    if (!m || m->item_count == 0) return;

    switch (key) {
        case UI_KEY_UP:
            if (m->selected > 0) m->selected--;
            break;
        case UI_KEY_DOWN:
            if (m->selected + 1 < m->item_count) m->selected++;
            break;
        case UI_KEY_OK: {
            ui_action_fn_t fn = m->items[m->selected].action;
            if (fn) fn(m->user_ctx);
            break;
        }
        case UI_KEY_BACK:
            // UI-only sample: you can hook this to go back to previous screen
            break;
        default:
            break;
    }

    clamp_scroll(m);
    m->dirty = true;
}

static void draw_title(ui_menu_t *m)
{
    u8g2_t *u = m->u;

    // White title bar
    u8g2_SetDrawColor(u, 1);
    u8g2_DrawBox(u, 0, 0, 128, UI_TITLE_H);

    // Black title text (erase pixels)
    u8g2_SetDrawColor(u, 0);
    u8g2_SetFont(u, FONT_TITLE);
    if (m->title) u8g2_DrawStr(u, 2, 10, m->title);

    // Black arrows
    u8g2_SetFont(u, FONT_SYMBOL);
    u8g2_DrawGlyph(u, 118, 10, 0x25B2);
    u8g2_DrawGlyph(u, 126, 10, 0x25BC);

    // Restore default
    u8g2_SetDrawColor(u, 1);

    // Optional line below title
    u8g2_DrawHLine(u, 0, UI_TITLE_H, 128);
}


static void draw_row(ui_menu_t *m, uint8_t row_idx, uint8_t item_idx)
{
    u8g2_t *u = m->u;
    const ui_menu_item_t *it = &m->items[item_idx];
    bool selected = (item_idx == m->selected);

    // Row geometry
    int y_top = UI_TITLE_H + 2 + (row_idx * UI_ROW_H);
    int y_base = y_top + 10;                 // baseline for text/glyphs
    int row_h = UI_ROW_H - 1;                // a bit tighter
    int x0 = 0, w = 128;

    if (selected) {
        // Draw a white bar (filled box)
        u8g2_SetDrawColor(u, 1);
        u8g2_DrawBox(u, x0, y_top, w, row_h);

        // Now draw "black" text/icon by erasing pixels inside the bar
        u8g2_SetDrawColor(u, 0);

        // Marker (optional) - use a small filled triangle look (▶) or just '>'
        u8g2_SetFont(u, FONT_SYMBOL);
        u8g2_DrawGlyph(u, 2, y_base, 0x25B6); // ▶

        // Icon
        if (it->glyph) {
            u8g2_SetFont(u, FONT_SYMBOL);
            u8g2_DrawGlyph(u, 16, y_base, it->glyph);
        }

        // Label
        u8g2_SetFont(u, FONT_TEXT);
        u8g2_DrawStr(u, 32, y_base, it->label ? it->label : "");

        // Restore normal draw color for subsequent items
        u8g2_SetDrawColor(u, 1);
    } else {
        // Normal row: just draw white pixels
        u8g2_SetDrawColor(u, 1);

        // Marker dot
        u8g2_SetFont(u, FONT_SYMBOL);
        u8g2_DrawGlyph(u, 4, y_base, 0x00B7); // ·

        // Icon
        if (it->glyph) {
            u8g2_SetFont(u, FONT_SYMBOL);
            u8g2_DrawGlyph(u, 16, y_base, it->glyph);
        }

        // Label
        u8g2_SetFont(u, FONT_TEXT);
        u8g2_DrawStr(u, 32, y_base, it->label ? it->label : "");
    }
}

void ui_menu_draw(ui_menu_t *m)
{
    if (!m || !m->u) return;
    if (!m->dirty) return;

    u8g2_t *u = m->u;

    u8g2_ClearBuffer(u);

    draw_title(m);

    // Draw visible items
    uint8_t count = m->item_count;
    uint8_t start = m->top;
    uint8_t end = start + UI_ROWS_VISIBLE;
    if (end > count) end = count;

    uint8_t row = 0;
    for (uint8_t i = start; i < end; i++, row++) {
        draw_row(m, row, i);
    }

    // Scroll indicator on right if needed
    if (count > UI_ROWS_VISIBLE) {
        u8g2_SetFont(u, FONT_SYMBOL);
        // Draw a small "track"
        u8g2_DrawVLine(u, 126, UI_TITLE_H + 2, 64 - (UI_TITLE_H + 2));

        // Thumb position (simple proportional)
        uint8_t track_h = 64 - (UI_TITLE_H + 4);
        uint8_t thumb_h = (track_h * UI_ROWS_VISIBLE) / count;
        if (thumb_h < 6) thumb_h = 6;

        uint8_t max_top = count - UI_ROWS_VISIBLE;
        uint8_t thumb_y = UI_TITLE_H + 2;
        if (max_top > 0) {
            thumb_y += (uint8_t)((track_h - thumb_h) * m->top / max_top);
        }
        // ■ U+25A0
        for (uint8_t k = 0; k < thumb_h; k += 2) {
            u8g2_DrawGlyph(u, 124, thumb_y + k, 0x25A0);
        }
    }

    u8g2_SendBuffer(u);
    m->dirty = false;
}
