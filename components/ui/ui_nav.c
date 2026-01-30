// ===================== components/ui/ui_nav.c =====================
#include "ui_nav.h"
#include "esp_log.h"
#include "u8g2.h"  // include only here

static const char *TAG = "ui_nav";

// Forward: screen registration function (implemented in ui_screens.c)
void ui_register_screens(ui_ctx_t *ctx);

static void call_enter(ui_ctx_t *ctx, ui_screen_t *s, void *param) {
    if (s && s->on_enter) s->on_enter(ctx, param);
}
static void call_exit(ui_ctx_t *ctx, ui_screen_t *s) {
    if (s && s->on_exit) s->on_exit(ctx);
}

void ui_nav_init(ui_ctx_t *ctx, void *u8g2_ptr) {
    ctx->u8g2 = u8g2_ptr;
    ctx->top = -1;
    ctx->dirty = true;
    ui_register_screens(ctx);
}

ui_screen_t *ui_nav_current(ui_ctx_t *ctx) {
    if (ctx->top < 0) return NULL;
    ui_screen_id_t id = ctx->stack[ctx->top].id;
    return &ctx->screens[id];
}

bool ui_nav_push(ui_ctx_t *ctx, ui_screen_id_t id, void *param) {
    if (ctx->top >= UI_STACK_MAX - 1) {
        ESP_LOGW(TAG, "stack full");
        return false;
    }
    ui_screen_t *cur = ui_nav_current(ctx);
    // no exit on push; keep underlying screen state as-is
    ctx->top++;
    ctx->stack[ctx->top] = (ui_stack_entry_t){ .id = id, .param = param };
    call_enter(ctx, &ctx->screens[id], param);
    ctx->dirty = true;
    return true;
}

bool ui_nav_pop(ui_ctx_t *ctx) {
    if (ctx->top <= 0) { // keep at least one screen
        return false;
    }
    ui_screen_t *cur = ui_nav_current(ctx);
    call_exit(ctx, cur);
    ctx->top--;
    ctx->dirty = true;
    return true;
}

bool ui_nav_replace(ui_ctx_t *ctx, ui_screen_id_t id, void *param) {
    if (ctx->top < 0) return ui_nav_push(ctx, id, param);
    ui_screen_t *cur = ui_nav_current(ctx);
    call_exit(ctx, cur);
    ctx->stack[ctx->top] = (ui_stack_entry_t){ .id = id, .param = param };
    call_enter(ctx, &ctx->screens[id], param);
    ctx->dirty = true;
    return true;
}

void ui_nav_dispatch(ui_ctx_t *ctx, const ui_event_t *ev) {
    ui_screen_t *cur = ui_nav_current(ctx);
    if (!cur || !cur->handle) return;

    cur->handle(ctx, ev);

    // Any interaction usually implies redraw
    if (ev->type != UI_EV_NONE) ctx->dirty = true;
}



static void ui_draw_ir_banner(ui_ctx_t *ctx, u8g2_t *u8)
{
    if (!ctx->ir_banner.show) return;

    char line[24];
    snprintf(line, sizeof(line), "IR %04X %04X",
             ctx->ir_banner.last_code.addr,
             ctx->ir_banner.last_code.cmd);

    // bottom banner (128x16)
    u8g2_SetDrawColor(u8, 1);
    u8g2_DrawBox(u8, 0, 64-16, 128, 16);
    u8g2_SetDrawColor(u8, 0);
    u8g2_SetFont(u8, u8g2_font_6x12_tf);
    u8g2_DrawStr(u8, 2, 64-4, line);
    u8g2_SetDrawColor(u8, 1);
}

void ui_nav_render_if_dirty(ui_ctx_t *ctx) {
    if (!ctx->dirty) return;

    u8g2_t *u8 = (u8g2_t *)ctx->u8g2;
    ui_screen_t *cur = ui_nav_current(ctx);
    if (!cur || !cur->render) return;

    u8g2_FirstPage(u8);
    do {
        cur->render(ctx, u8);

        // Just for banner overlay
        ui_draw_ir_banner(ctx, u8);
    } while (u8g2_NextPage(u8));

    ctx->dirty = false;
}
