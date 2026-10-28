#include "ui_internal.h"
#include "esp_app_desc.h"

void ui_settings_handle(ui_ctx_t *ctx, const ui_event_t *event)
{
    if (event->type == UI_EV_UP && ctx->settings_sel) --ctx->settings_sel;
    if (event->type == UI_EV_DOWN && ctx->settings_sel < 1) ++ctx->settings_sel;
    if (event->type == UI_EV_BACK) ui_nav_pop(ctx);
    if (event->type == UI_EV_OK && !event->repeat)
        ui_nav_push(ctx, ctx->settings_sel ? UI_SCREEN_STORAGE : UI_SCREEN_ABOUT, NULL);
}

void ui_settings_render(ui_ctx_t *ctx, u8g2_t *u8)
{
    ui_title(u8, "Settings", NULL);
    ui_row(u8, 32, "About", ctx->settings_sel == 0);
    ui_row(u8, 54, "Storage", ctx->settings_sel == 1);
    ui_footer(ctx, u8, "OK:open", "BACK:browse");
}

void ui_about_render(ui_ctx_t *ctx, u8g2_t *u8)
{
    ui_title(u8, "About", NULL);
    ui_draw_product_logo(u8, 16, 19);
    u8g2_SetFont(u8, u8g2_font_6x10_tf);
    ui_center(u8, 63, "UNIVERSAL");
    ui_center(u8, 75, "REMOTE");
    u8g2_SetFont(u8, u8g2_font_5x8_tf);
    ui_center(u8, 85, "Firmware");
    // ESP-IDF embeds the actual project/build version in the running image.
    // Wrap its full 31-character value instead of clipping long build IDs.
    const char *version = esp_app_get_description()->version;
    size_t length = strlen(version);
    for (size_t offset = 0; offset < length; offset += 12) {
        char line[13];
        snprintf(line, sizeof(line), "%.12s", version + offset);
        ui_center(u8, 93 + (offset / 12) * 8, line);
    }
    ui_footer(ctx, u8, "BACK:settings", "HOME:browse");
}

void ui_storage_handle(ui_ctx_t *ctx, const ui_event_t *event)
{
    if (event->type == UI_EV_BACK) ui_nav_pop(ctx);
    if (event->type == UI_EV_OK && !event->repeat) ctx->storage_pending = true;
}

void ui_storage_render(ui_ctx_t *ctx, u8g2_t *u8)
{
    ui_title(u8, "Storage", NULL);
    u8g2_SetFont(u8, u8g2_font_6x10_tf);
    if (ctx->storage_pending) {
        ui_center(u8, 56, "Reading...");
    } else if (ctx->storage_status != ESP_OK) {
        ui_center(u8, 48, "Storage");
        ui_center(u8, 60, "unavailable");
        u8g2_SetFont(u8, u8g2_font_5x8_tf);
        ui_center(u8, 83, "OK to retry");
    } else {
        char line[24];
        ui_center(u8, 31, "Remotes");
        snprintf(line, sizeof(line), "%lu B used", (unsigned long)ctx->storage_usage.remote_bytes);
        ui_center(u8, 45, line);
        ui_center(u8, 67, "Free space");
        snprintf(line, sizeof(line), "%lu B", (unsigned long)ctx->storage_usage.free_bytes);
        ui_center(u8, 81, line);
        u8g2_SetFont(u8, u8g2_font_5x8_tf);
        ui_center(u8, 101, "Shared store");
    }
    ui_footer(ctx, u8, "OK:refresh", "BACK:settings");
}
