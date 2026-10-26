#include "ui_nav.h"
#include <string.h>
#include "u8g2.h"

void ui_register_screens(ui_ctx_t *ctx);

void ui_nav_init(ui_ctx_t *ctx, void *display)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->u8g2 = display;
    ctx->top = -1;
    ctx->back_target_top = -1;
    ctx->dirty = true;
    ui_register_screens(ctx);
}
ui_screen_t *ui_nav_current(ui_ctx_t *ctx)
{
    return ctx->top < 0 ? NULL : &ctx->screens[ctx->stack[ctx->top].id];
}
bool ui_nav_push(ui_ctx_t *ctx, ui_screen_id_t id, void *param)
{
    if (id >= UI_SCREEN_COUNT || ctx->top >= UI_STACK_MAX - 1) return false;
    ctx->stack[++ctx->top].id = id;
    if (ctx->screens[id].on_enter) ctx->screens[id].on_enter(ctx, param);
    ctx->dirty = true;
    return true;
}
bool ui_nav_pop(ui_ctx_t *ctx)
{
    if (ctx->top <= 0) return false;
    ui_screen_t *current = ui_nav_current(ctx);
    if (current->on_exit) current->on_exit(ctx);
    --ctx->top;
    ctx->notice[0] = 0;
    ctx->dirty = true;
    return true;
}
bool ui_nav_replace(ui_ctx_t *ctx, ui_screen_id_t id, void *param)
{
    if (id >= UI_SCREEN_COUNT) return false;
    if (ctx->top < 0) return ui_nav_push(ctx, id, param);
    ui_screen_t *current = ui_nav_current(ctx);
    if (current->on_exit) current->on_exit(ctx);
    ctx->stack[ctx->top].id = id;
    if (ctx->screens[id].on_enter) ctx->screens[id].on_enter(ctx, param);
    ctx->notice[0] = 0;
    ctx->dirty = true;
    return true;
}
void ui_nav_home(ui_ctx_t *ctx)
{
    while (ctx->top >= 0) {
        ui_screen_t *current = ui_nav_current(ctx);
        if (current->on_exit) current->on_exit(ctx);
        --ctx->top;
    }
    ctx->editing = false;
    memset(&ctx->draft, 0, sizeof(ctx->draft));
    ctx->notice[0] = 0;
    unsigned count = app_remote_count();
    if (ctx->home_sel > count) ctx->home_sel = count;
    ui_nav_push(ctx, UI_SCREEN_HOME, NULL);
}
void ui_nav_dispatch(ui_ctx_t *ctx, const ui_event_t *event)
{
    ui_screen_t *current = ui_nav_current(ctx);
    if (!current) return;
    bool learning_screen = current->id == UI_SCREEN_IR_KEYS
        || current->id == UI_SCREEN_IR_REPLACE || current->id == UI_SCREEN_IR_WAIT
        || current->id == UI_SCREEN_IR_RESULT;
    if (event->type == UI_EV_BACK && !event->repeat) {
        ctx->back_target_top = learning_screen ? ctx->top - 1 : -1;
        // Short BACK selects its binding on the key picker. On IR details it
        // waits for the hold, so pressing BACK cannot cancel its own recording.
        if (learning_screen && current->id != UI_SCREEN_IR_KEYS) return;
    }
    if (event->type == UI_EV_BACK_LONG && learning_screen && ctx->back_target_top >= 0) {
        // A hold may have opened the BACK recording screen on its initial
        // press. Return relative to the original screen, avoiding that detour.
        int target = ctx->back_target_top;
        ctx->back_target_top = -1;
        while (ctx->top > target) ui_nav_pop(ctx);
        return;
    }
    // Remote mode owns HOME/BACK as appliance commands. In the key picker,
    // short HOME is learnable; a hold always finishes setup.
    if (current->id != UI_SCREEN_SPLASH && current->id != UI_SCREEN_REMOTE
        && (event->type == UI_EV_HOME_LONG
            || (event->type == UI_EV_HOME && current->id != UI_SCREEN_IR_KEYS))) {
        ui_nav_home(ctx);
        return;
    }
    if (current->handle) current->handle(ctx, event);
    if (event->type != UI_EV_NONE && event->type != UI_EV_TICK) ctx->dirty = true;
}
void ui_nav_render_if_dirty(ui_ctx_t *ctx)
{
    if (!ctx->dirty) return;
    u8g2_t *display = ctx->u8g2;
    ui_screen_t *current = ui_nav_current(ctx);
    if (!current || !current->render) return;
    u8g2_ClearBuffer(display);
    u8g2_SetDrawColor(display, 1);
    current->render(ctx, display);
    u8g2_SendBuffer(display);
    ctx->dirty = false;
}
