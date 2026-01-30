#include "app_state.h"
#include <string.h>

static app_ctx_t s_app;

app_ctx_t *app_get_ctx(void) { return &s_app; }

void app_post_evt(const app_evt_t *e) {
    if (!s_app.evt_q) return;
    (void)xQueueSend(s_app.evt_q, e, 0);
}

bool app_send_cmd(const app_cmd_t *c, TickType_t to) {
    if (!s_app.cmd_q) return false;
    return xQueueSend(s_app.cmd_q, c, to) == pdTRUE;
}

bool app_read_evt(app_evt_t *e, TickType_t to) {
    return (xQueueReceive(s_app.evt_q, e, to) == pdTRUE);
}

// call this once in app_main()
void app_state_init(void) {
    memset(&s_app, 0, sizeof(s_app));
    s_app.cmd_q = xQueueCreate(16, sizeof(app_cmd_t));
    s_app.evt_q = xQueueCreate(16, sizeof(app_evt_t));
    s_app.model_mutex = xSemaphoreCreateMutex();
}
