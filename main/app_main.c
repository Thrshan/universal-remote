#include "keypad.h"
#include "app_ui_task.h"
#include "app_state.h"
#include "ir_nec.h"
#include "bt_runtime.h"

void app_main(void) {
    app_state_init();
    bt_runtime_init();
    ir_start_tasks();
    app_ui_start();
    QueueHandle_t q = ui_get_queue();
    start_keypad_task(q);
}
