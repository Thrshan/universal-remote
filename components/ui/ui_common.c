#include "ui_internal.h"
#include "esp_timer.h"

uint32_t ui_now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000ULL); }

ui_screen_id_t ui_screen_id(ui_ctx_t *ctx) { return ui_nav_current(ctx)->id; }

void ui_notice(ui_ctx_t *ctx, const char *message)
{
    snprintf(ctx->notice, sizeof(ctx->notice), "%s", message);
    ctx->notice_until = ui_now_ms() + 2500;
    ctx->dirty = true;
}

void ui_center(u8g2_t *u8, int y, const char *text)
{
    const uint8_t *font = u8->font;
    // Keep long names readable in full instead of clipping the larger font.
    if (u8g2_GetStrWidth(u8, text) > UI_WIDTH - 4) u8g2_SetFont(u8, u8g2_font_5x8_tf);
    if (u8g2_GetStrWidth(u8, text) > UI_WIDTH - 4) u8g2_SetFont(u8, u8g2_font_4x6_tf);
    int width = u8g2_GetStrWidth(u8, text);
    u8g2_DrawStr(u8, width < UI_WIDTH ? (UI_WIDTH - width) / 2 : 0, y, text);
    u8g2_SetFont(u8, font);
}

void ui_title(u8g2_t *u8, const char *text, const char *right)
{
    u8g2_SetFont(u8, u8g2_font_5x8_tf);
    u8g2_DrawStr(u8, 2, 10, text);
    if (right) {
        u8g2_SetFont(u8, u8g2_font_4x6_tf);
        u8g2_DrawStr(u8, UI_WIDTH - u8g2_GetStrWidth(u8, right) - 1, 9, right);
    }
    u8g2_DrawHLine(u8, 0, 14, UI_WIDTH);
}

void ui_footer(ui_ctx_t *ctx, u8g2_t *u8, const char *first, const char *second)
{
    u8g2_DrawHLine(u8, 0, 111, UI_WIDTH);
    u8g2_SetFont(u8, u8g2_font_5x8_tf);
    ui_center(u8, 119, ctx->notice[0] ? ctx->notice : first);
    ui_center(u8, 126, second);
}

void ui_row(u8g2_t *u8, int y, const char *text, bool selected)
{
    if (selected) {
        u8g2_DrawBox(u8, 1, y, 62, 16);
        u8g2_SetDrawColor(u8, 0);
    }
    u8g2_SetFont(u8, u8g2_font_6x10_tf);
    if (u8g2_GetStrWidth(u8, text) > 56) u8g2_SetFont(u8, u8g2_font_5x8_tf);
    if (u8g2_GetStrWidth(u8, text) > 56) u8g2_SetFont(u8, u8g2_font_4x6_tf);
    u8g2_DrawStr(u8, 4, y + 11, text);
    u8g2_SetDrawColor(u8, 1);
}

void ui_plus_icon(u8g2_t *u8, int x, int y)
{
    u8g2_DrawFrame(u8, x + 1, y + 1, 14, 14);
    u8g2_DrawHLine(u8, x + 4, y + 8, 8);
    u8g2_DrawVLine(u8, x + 8, y + 4, 8);
}
