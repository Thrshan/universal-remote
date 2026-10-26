// Render the applied typography through the production screen code.
#include "ui_nav.h"
#include "oled_port.h"
#include "host_support.h"
#include <assert.h>
#include <stdio.h>

static u8g2_t display;
static uint8_t dummy(u8x8_t *u8, uint8_t message, uint8_t argument, void *pointer)
{ (void)u8; (void)message; (void)argument; (void)pointer; return 1; }
static void save(const char *name)
{
    char path[256];
    snprintf(path, sizeof(path), "build_host/typography/%s.pbm", name);
    FILE *file = fopen(path, "w");
    assert(file);
    fprintf(file, "P1\n64 128\n");
    const uint8_t *buffer = u8g2_GetBufferPtr(&display);
    for (unsigned y = 0; y < 128; ++y) {
        for (unsigned x = 0; x < 64; ++x) {
            unsigned lit = (buffer[(x / 8) * 128 + 127 - y] >> (x % 8)) & 1;
            fprintf(file, "%u ", !lit);
        }
        fputc('\n', file);
    }
    fclose(file);
}
int main(void)
{
    host_clear_storage();
    app_state_init();
    remote_t remote = {.name = "Living TV", .transport = REMOTE_IR};
    assert(app_remote_create(&remote, NULL) == ESP_OK);
    oled_setup_portrait(&display, dummy, dummy);
    ui_ctx_t ctx;
    ui_nav_init(&ctx, &display);
    ui_nav_push(&ctx, UI_SCREEN_HOME, NULL);
    ui_nav_render_if_dirty(&ctx);
    save("home-after");
    ctx.active_remote = 0;
    ctx.ir.key = REMOTE_KEY_BACK;
    ctx.ir.status = UI_IR_WAITING;
    ui_nav_push(&ctx, UI_SCREEN_IR_WAIT, NULL);
    ui_nav_render_if_dirty(&ctx);
    save("ir-after");
    return 0;
}
