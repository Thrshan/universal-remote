// ===================== main/app_main.c =====================
#include "esp_log.h"
#include "keypad.h"
#include "app_ui_task.h"
#include "app_state.h"
#include "ir_nec.h"

void app_main(void) {
    // init i2c + u8g2 glue (your existing code)
    // init keypad/IR/BLE tasks that send ui_event_t to ui_get_queue()
    // start_keypad_task();
    app_state_init();
    ir_start_tasks();
    app_ui_start();
    QueueHandle_t q = ui_get_queue();
    start_keypad_task((void*)q);
}
