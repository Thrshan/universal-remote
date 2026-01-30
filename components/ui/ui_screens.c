// ===================== components/ui/ui_screens.c =====================
#include "ui_nav.h"
#include "u8g2.h"
#include <stdio.h>

// ---- Helpers ----
static void draw_title(u8g2_t *u8, const char *t) {
    u8g2_SetFont(u8, u8g2_font_6x12_tf);
    u8g2_DrawStr(u8, 0, 12, t);
    u8g2_DrawHLine(u8, 0, 14, 128);
}

// ---- HOME screen ----
static void home_enter(ui_ctx_t *ctx, void *param) { (void)param; ctx->st.home.sel = 0; }
static void home_exit(ui_ctx_t *ctx) { (void)ctx; }

static void home_handle(ui_ctx_t *ctx, const ui_event_t *ev) {
    switch (ev->type) {
        case UI_EV_LEFT:  if (ctx->st.home.sel > 0) ctx->st.home.sel--; break;
        case UI_EV_RIGHT: if (ctx->st.home.sel < 3) ctx->st.home.sel++; break;
        case UI_EV_UP:    if (ctx->st.home.sel >= 2) ctx->st.home.sel -= 2; break;
        case UI_EV_DOWN:  if (ctx->st.home.sel <= 1) ctx->st.home.sel += 2; break;
        case UI_EV_OK:
            if (ctx->st.home.sel == 0) ui_nav_push(ctx, UI_SCREEN_IR_LEARN, NULL);
            if (ctx->st.home.sel == 1) ui_nav_push(ctx, UI_SCREEN_IR_LIST, NULL);
            if (ctx->st.home.sel == 2) ui_nav_push(ctx, UI_SCREEN_MENU, NULL);
            if (ctx->st.home.sel == 3) ui_nav_push(ctx, UI_SCREEN_DIALOG, (void*)"Universal Remote");
            break;
        default: break;
    }
}

static void draw_icon_box(u8g2_t *u8, int x, int y, int w, int h, bool sel, const char *label) {
    if (sel) {
        u8g2_DrawBox(u8, x, y, w, h);
        u8g2_SetDrawColor(u8, 0);
    } else {
        u8g2_DrawFrame(u8, x, y, w, h);
    }

    u8g2_SetFont(u8, u8g2_font_5x8_tf);
    u8g2_DrawStr(u8, x + 4, y + h - 4, label);

    // reset draw color for next draws
    u8g2_SetDrawColor(u8, 1);
}

static void home_render(ui_ctx_t *ctx, u8g2_t *u8) {
    draw_title(u8, "Home");

    // 2x2 grid of boxes
    const int x0 = 4, y0 = 18, bw = 60, bh = 20, gap = 4;

    draw_icon_box(u8, x0,              y0,              bw, bh, ctx->st.home.sel==0, "Add IR");
    draw_icon_box(u8, x0 + bw + gap,   y0,              bw, bh, ctx->st.home.sel==1, "Send IR");
    draw_icon_box(u8, x0,              y0 + bh + gap,   bw, bh, ctx->st.home.sel==2, "Menu");
    draw_icon_box(u8, x0 + bw + gap,   y0 + bh + gap,   bw, bh, ctx->st.home.sel==3, "About");
}
// ================= IR LEARN screen =================
static void ir_learn_enter(ui_ctx_t *ctx, void *param)
{
    (void)param;
    ctx->st.ir_learn.waiting = true;
    ctx->st.ir_learn.done = false;
    ctx->st.ir_learn.timeout = false;

    // Fire one-shot learn
    app_cmd_t c = {.type = APP_CMD_IR_LEARN_ONESHOT};
    c.learn.timeout_ms = 5000;
    app_send_cmd(&c, 0);
}

static void ir_learn_exit(ui_ctx_t *ctx) { (void)ctx; }

static void ir_learn_handle(ui_ctx_t *ctx, const ui_event_t *ev)
{
    if (ev->type == UI_EV_BACK) {
        ui_nav_pop(ctx);
        return;
    }
    if (ev->type == UI_EV_OK && ctx->st.ir_learn.timeout) {
        // re-record on OK after timeout
        app_cmd_t c = {.type = APP_CMD_IR_LEARN_ONESHOT};
        c.learn.timeout_ms = 5000;
        app_send_cmd(&c, 0);
        ctx->st.ir_learn.timeout = false;
        ctx->st.ir_learn.waiting = true;
        ctx->st.ir_learn.done = false;
    }
}

static void ir_learn_render(ui_ctx_t *ctx, u8g2_t *u8)
{
    draw_title(u8, "Learn IR");
    u8g2_SetFont(u8, u8g2_font_6x12_tf);

    app_ctx_t *g = app_get_ctx();

    // Detect latest event snapshot (simple approach)
    if (g->last_evt.type == APP_EVT_IR_LEARNED) {
        ctx->st.ir_learn.waiting = false;
        ctx->st.ir_learn.done = true;
        ctx->st.ir_learn.timeout = false;
        ctx->st.ir_learn.last = g->last_evt.ir.code;
    } else if (g->last_evt.type == APP_EVT_IR_LEARN_TIMEOUT) {
        ctx->st.ir_learn.waiting = false;
        ctx->st.ir_learn.done = false;
        ctx->st.ir_learn.timeout = true;
    }

    if (ctx->st.ir_learn.waiting) {
        u8g2_DrawStr(u8, 2, 32, "Press remote key...");
        u8g2_DrawStr(u8, 2, 52, "BACK: Cancel");
    } else if (ctx->st.ir_learn.done) {
        char line[32];
        snprintf(line, sizeof(line), "A:0x%02X C:0x%02X",
                 (unsigned)ctx->st.ir_learn.last.addr,
                 (unsigned)ctx->st.ir_learn.last.cmd);
        u8g2_DrawStr(u8, 2, 28, "Recorded!");
        u8g2_DrawStr(u8, 2, 42, line);
        u8g2_DrawStr(u8, 2, 60, "BACK: Home");
    } else if (ctx->st.ir_learn.timeout) {
        u8g2_DrawStr(u8, 2, 32, "Timeout / invalid");
        u8g2_DrawStr(u8, 2, 52, "OK: Re-try  BACK");
    }
}


// ================= IR LIST screen =================
static void ir_list_enter(ui_ctx_t *ctx, void *param)
{
    (void)param;
    ctx->st.ir_list.sel = 0;
    ctx->st.ir_list.scroll = 0;
}

static void ir_list_exit(ui_ctx_t *ctx) { (void)ctx; }

static void ir_list_handle(ui_ctx_t *ctx, const ui_event_t *ev)
{
    app_ctx_t *g = app_get_ctx();

    xSemaphoreTake(g->model_mutex, portMAX_DELAY);
    uint8_t count = g->ir_count;
    xSemaphoreGive(g->model_mutex);

    switch (ev->type) {
        case UI_EV_UP:
            if (ctx->st.ir_list.sel > 0) ctx->st.ir_list.sel--;
            break;
        case UI_EV_DOWN:
            if (count && ctx->st.ir_list.sel < count - 1) ctx->st.ir_list.sel++;
            break;
        case UI_EV_BACK:
            ui_nav_pop(ctx);
            break;
        case UI_EV_OK: {
            if (!count) break;
            ir_nec_code_t code;
            xSemaphoreTake(g->model_mutex, portMAX_DELAY);
            code = g->ir_slots[ctx->st.ir_list.sel].code;
            xSemaphoreGive(g->model_mutex);

            app_cmd_t c = {.type = APP_CMD_IR_TX_SEND};
            c.tx.code = code;
            app_send_cmd(&c, 0);
            break;
        }
        default: break;
    }
}

static void ir_list_render(ui_ctx_t *ctx, u8g2_t *u8)
{
    draw_title(u8, "Send IR");
    u8g2_SetFont(u8, u8g2_font_6x12_tf);

    app_ctx_t *g = app_get_ctx();
    uint8_t count;

    xSemaphoreTake(g->model_mutex, portMAX_DELAY);
    count = g->ir_count;
    xSemaphoreGive(g->model_mutex);

    if (count == 0) {
        u8g2_DrawStr(u8, 2, 32, "No IR recorded");
        u8g2_DrawStr(u8, 2, 52, "BACK: Home");
        return;
    }

    const int row_h = 12;
    const int start_y = 28;
    const int max_rows = 3;

    int first = ctx->st.ir_list.scroll;
    if (ctx->st.ir_list.sel < first) first = ctx->st.ir_list.sel;
    if (ctx->st.ir_list.sel >= first + max_rows) first = ctx->st.ir_list.sel - max_rows + 1;
    ctx->st.ir_list.scroll = first;

    for (int i = 0; i < max_rows; i++) {
        int idx = first + i;
        if (idx >= count) break;

        int y = start_y + i * row_h;
        bool sel = (idx == ctx->st.ir_list.sel);

        char line[20];
        xSemaphoreTake(g->model_mutex, portMAX_DELAY);
        const ir_slot_t *s = &g->ir_slots[idx];
        snprintf(line, sizeof(line), "%s %02X %02X", s->name, s->code.addr, s->code.cmd);
        xSemaphoreGive(g->model_mutex);

        if (sel) { u8g2_DrawBox(u8, 0, y - 10, 128, 12); u8g2_SetDrawColor(u8, 0); }
        u8g2_DrawStr(u8, 2, y, line);
        if (sel) u8g2_SetDrawColor(u8, 1);
    }

    u8g2_SetFont(u8, u8g2_font_5x8_tf);
    u8g2_DrawStr(u8, 2, 62, "OK: Send  BACK");
}

// ---- MENU screen ----
static const char *menu_items[] = {"Settings", "Display", "Sound", "Back"};
static const int menu_count = 4;

static void menu_enter(ui_ctx_t *ctx, void *param) { (void)param; ctx->st.menu.sel=0; ctx->st.menu.scroll=0; }
static void menu_exit(ui_ctx_t *ctx) { (void)ctx; }

static void menu_handle(ui_ctx_t *ctx, const ui_event_t *ev) {
    switch (ev->type) {
        case UI_EV_UP:
            if (ctx->st.menu.sel > 0) ctx->st.menu.sel--;
            break;
        case UI_EV_DOWN:
            if (ctx->st.menu.sel < (menu_count - 1)) ctx->st.menu.sel++;
            break;
        case UI_EV_BACK:
            ui_nav_pop(ctx);
            break;
        case UI_EV_OK:
            if (ctx->st.menu.sel == menu_count - 1) ui_nav_pop(ctx);
            break;
        default: break;
    }
}

static void menu_render(ui_ctx_t *ctx, u8g2_t *u8) {
    draw_title(u8, "Menu");

    u8g2_SetFont(u8, u8g2_font_6x12_tf);

    // show up to 4 rows
    const int row_h = 12;
    const int start_y = 28;
    const int max_rows = 3; // (64-16)/12 ~ 4, keeping spacing
    int first = ctx->st.menu.scroll;

    // adjust scroll to keep selection visible
    if (ctx->st.menu.sel < first) first = ctx->st.menu.sel;
    if (ctx->st.menu.sel >= first + max_rows) first = ctx->st.menu.sel - max_rows + 1;
    ctx->st.menu.scroll = first;

    for (int i = 0; i < max_rows; i++) {
        int idx = first + i;
        if (idx >= menu_count) break;

        int y = start_y + i * row_h;

        bool sel = (idx == ctx->st.menu.sel);
        if (sel) {
            u8g2_DrawBox(u8, 0, y - 10, 128, 12);
            u8g2_SetDrawColor(u8, 0);
        }

        u8g2_DrawStr(u8, 2, y, menu_items[idx]);

        if (sel) u8g2_SetDrawColor(u8, 1);
    }
}

// ---- ITEMS screen (placeholder) ----
static void items_enter(ui_ctx_t *ctx, void *param) { (void)param; ctx->st.items.sel=0; ctx->st.items.scroll=0; }
static void items_exit(ui_ctx_t *ctx) { (void)ctx; }

static void items_handle(ui_ctx_t *ctx, const ui_event_t *ev) {
    switch (ev->type) {
        case UI_EV_BACK: ui_nav_pop(ctx); break;
        case UI_EV_OK:   ui_nav_push(ctx, UI_SCREEN_ADD_TYPE, NULL); break;
        default: break;
    }
}

static void items_render(ui_ctx_t *ctx, u8g2_t *u8) {
    draw_title(u8, "Items");
    u8g2_SetFont(u8, u8g2_font_6x12_tf);

    char buf[32];
    // NOTE: if you use snprintf, include stdio.h
    // Here just draw a placeholder:
    u8g2_DrawStr(u8, 2, 32, "List goes here");
    u8g2_DrawStr(u8, 2, 48, "OK: Add  BACK: Home");
    (void)buf;
}

// ---- ADD_TYPE screen (placeholder) ----
static const char *add_types[] = {"Timer", "Counter", "Shortcut", "Cancel"};
static const int add_type_count = 4;

static void add_type_enter(ui_ctx_t *ctx, void *param) { (void)param; ctx->st.add_type.sel = 0; }
static void add_type_exit(ui_ctx_t *ctx) { (void)ctx; }

static void add_type_handle(ui_ctx_t *ctx, const ui_event_t *ev) {
    switch (ev->type) {
        case UI_EV_UP:   if (ctx->st.add_type.sel>0) ctx->st.add_type.sel--; break;
        case UI_EV_DOWN: if (ctx->st.add_type.sel<add_type_count-1) ctx->st.add_type.sel++; break;
        case UI_EV_BACK: ui_nav_pop(ctx); break;
        case UI_EV_OK:
            if (ctx->st.add_type.sel == add_type_count-1) ui_nav_pop(ctx);
            else ui_nav_push(ctx, UI_SCREEN_ADD_EDIT, (void*)add_types[ctx->st.add_type.sel]);
            break;
        default: break;
    }
}

static void add_type_render(ui_ctx_t *ctx, u8g2_t *u8) {
    draw_title(u8, "Add: Type");
    u8g2_SetFont(u8, u8g2_font_6x12_tf);

    for (int i = 0; i < add_type_count; i++) {
        int y = 28 + i * 12;
        bool sel = (i == ctx->st.add_type.sel);
        if (sel) { u8g2_DrawBox(u8, 0, y - 10, 128, 12); u8g2_SetDrawColor(u8, 0); }
        u8g2_DrawStr(u8, 2, y, add_types[i]);
        if (sel) u8g2_SetDrawColor(u8, 1);
    }
}

// ---- ADD_EDIT screen (placeholder) ----
static void add_edit_enter(ui_ctx_t *ctx, void *param) {
    // param: selected type string
    ctx->st.add_edit.field = 0;
    ctx->st.add_edit.value = 10;
    (void)param;
}
static void add_edit_exit(ui_ctx_t *ctx) { (void)ctx; }

static void add_edit_handle(ui_ctx_t *ctx, const ui_event_t *ev) {
    switch (ev->type) {
        case UI_EV_LEFT:  ctx->st.add_edit.value--; break;
        case UI_EV_RIGHT: ctx->st.add_edit.value++; break;
        case UI_EV_BACK:  ui_nav_pop(ctx); break;
        case UI_EV_OK:
            // Here you would commit to model + pop to previous screen(s)
            ctx->model.item_count++;
            ui_nav_pop(ctx); // back to type
            ui_nav_pop(ctx); // back to previous
            break;
        default: break;
    }
}

static void add_edit_render(ui_ctx_t *ctx, u8g2_t *u8) {
    draw_title(u8, "Add: Edit");
    u8g2_SetFont(u8, u8g2_font_6x12_tf);

    u8g2_DrawStr(u8, 2, 32, "Value:");
    char tmp[16];
    // include <stdio.h> if you actually use snprintf
    // snprintf(tmp, sizeof(tmp), "%ld", (long)ctx->st.add_edit.value);
    // For placeholder:
    u8g2_DrawStr(u8, 60, 32, "<num>");
    u8g2_DrawStr(u8, 2, 52, "OK: Save  BACK: Cancel");
    (void)tmp;
}

// ---- DIALOG screen (placeholder) ----
static void dialog_enter(ui_ctx_t *ctx, void *param) {
    ctx->st.dialog.title = "Info";
    ctx->st.dialog.msg = (const char *)param;
    ctx->st.dialog.sel = 0;
}
static void dialog_exit(ui_ctx_t *ctx) { (void)ctx; }

static void dialog_handle(ui_ctx_t *ctx, const ui_event_t *ev) {
    switch (ev->type) {
        case UI_EV_OK:
        case UI_EV_BACK:
            ui_nav_pop(ctx);
            break;
        default: break;
    }
}

static void dialog_render(ui_ctx_t *ctx, u8g2_t *u8) {
    // simple centered box
    u8g2_DrawFrame(u8, 8, 16, 112, 40);
    u8g2_SetFont(u8, u8g2_font_6x12_tf);
    u8g2_DrawStr(u8, 14, 30, ctx->st.dialog.title);
    u8g2_SetFont(u8, u8g2_font_5x8_tf);
    u8g2_DrawStr(u8, 14, 44, ctx->st.dialog.msg ? ctx->st.dialog.msg : "...");
    u8g2_DrawStr(u8, 14, 56, "OK/BACK");
}

// ---- Registration ----
void ui_register_screens(ui_ctx_t *ctx) {
    ctx->screens[UI_SCREEN_HOME] = (ui_screen_t){
        .id=UI_SCREEN_HOME, .on_enter=home_enter, .on_exit=home_exit,
        .handle=home_handle, .render=home_render, .state=&ctx->st.home
    };
    ctx->screens[UI_SCREEN_MENU] = (ui_screen_t){
        .id=UI_SCREEN_MENU, .on_enter=menu_enter, .on_exit=menu_exit,
        .handle=menu_handle, .render=menu_render, .state=&ctx->st.menu
    };
    ctx->screens[UI_SCREEN_ITEMS] = (ui_screen_t){
        .id=UI_SCREEN_ITEMS, .on_enter=items_enter, .on_exit=items_exit,
        .handle=items_handle, .render=items_render, .state=&ctx->st.items
    };
    ctx->screens[UI_SCREEN_ADD_TYPE] = (ui_screen_t){
        .id=UI_SCREEN_ADD_TYPE, .on_enter=add_type_enter, .on_exit=add_type_exit,
        .handle=add_type_handle, .render=add_type_render, .state=&ctx->st.add_type
    };
    ctx->screens[UI_SCREEN_ADD_EDIT] = (ui_screen_t){
        .id=UI_SCREEN_ADD_EDIT, .on_enter=add_edit_enter, .on_exit=add_edit_exit,
        .handle=add_edit_handle, .render=add_edit_render, .state=&ctx->st.add_edit
    };
    ctx->screens[UI_SCREEN_DIALOG] = (ui_screen_t){
        .id=UI_SCREEN_DIALOG, .on_enter=dialog_enter, .on_exit=dialog_exit,
        .handle=dialog_handle, .render=dialog_render, .state=&ctx->st.dialog
    };

    ctx->screens[UI_SCREEN_IR_LEARN] = (ui_screen_t){
        .id=UI_SCREEN_IR_LEARN, .on_enter=ir_learn_enter, .on_exit=ir_learn_exit,
        .handle=ir_learn_handle, .render=ir_learn_render, .state=&ctx->st.ir_learn
    };

    ctx->screens[UI_SCREEN_IR_LIST] = (ui_screen_t){
        .id=UI_SCREEN_IR_LIST, .on_enter=ir_list_enter, .on_exit=ir_list_exit,
        .handle=ir_list_handle, .render=ir_list_render, .state=&ctx->st.ir_list
    };
}
