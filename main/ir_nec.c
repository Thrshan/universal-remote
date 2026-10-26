// ===================== main/ir_nec.c =====================
#include "ir_nec.h"
#include "app_state.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

#include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"
#include "ir_nec_encoder.h"

static const char *TAG = "ir";

#define IR_TX_GPIO  18
#define IR_RX_GPIO  2
#define RMT_RES_HZ  1000000

static rmt_channel_handle_t s_tx_chan;
static rmt_channel_handle_t s_rx_chan;
static rmt_encoder_handle_t s_nec_encoder;

typedef struct { size_t num_symbols; } rx_done_msg_t;
static QueueHandle_t s_rx_done_q;

static rmt_symbol_word_t s_rx_buf[64];

static inline bool in_range(uint32_t v, uint32_t lo, uint32_t hi) { return v>=lo && v<=hi; }

#define NEC_MARGIN 200
#define NEC_LEAD0  9000
#define NEC_LEAD1  4500
#define NEC_0_0    560
#define NEC_0_1    560
#define NEC_1_0    560
#define NEC_1_1    1690

static inline bool near(uint32_t v, uint32_t spec) {
    return (v > (spec - NEC_MARGIN)) && (v < (spec + NEC_MARGIN));
}
static inline bool is0(const rmt_symbol_word_t *s) {
    return near(s->duration0, NEC_0_0) && near(s->duration1, NEC_0_1);
}
static inline bool is1(const rmt_symbol_word_t *s) {
    return near(s->duration0, NEC_1_0) && near(s->duration1, NEC_1_1);
}

static void debug_print_raw_symbols(const rmt_symbol_word_t *syms, size_t n)
{
    ESP_LOGI(TAG, "---- RAW RMT (%u symbols) ----", (unsigned)n);
    for (size_t i = 0; i < n; i++) {
        ESP_LOGI(TAG,
            "[%02u] L0=%u D0=%4u us | L1=%u D1=%4u us",
            (unsigned)i,
            syms[i].level0,
            syms[i].duration0,
            syms[i].level1,
            syms[i].duration1
        );
    }
    ESP_LOGI(TAG, "----------------------------");
}

static bool nec_try_decode(const rmt_symbol_word_t *syms, size_t n, ir_nec_code_t *out)
{
    if (!syms || !out) return false;
    if (n < 34) return false;

    if (!near(syms[0].duration0, NEC_LEAD0) || !near(syms[0].duration1, NEC_LEAD1)) {
        return false;
    }

    uint16_t addr = 0;
    uint16_t cmd  = 0;

    // next 16 bits = address (LSB first)
    for (int i = 0; i < 16; i++) {
        const rmt_symbol_word_t *s = &syms[1 + i];
        if (is1(s)) addr |= (1u << i);
        else if (is0(s)) addr &= ~(1u << i);
        else return false;
    }

    // next 16 bits = command (LSB first)
    for (int i = 0; i < 16; i++) {
        const rmt_symbol_word_t *s = &syms[1 + 16 + i];
        if (is1(s)) cmd |= (1u << i);
        else if (is0(s)) cmd &= ~(1u << i);
        else return false;
    }

    out->addr = addr;
    out->cmd  = cmd;
    return true;
}

static bool on_rx_done(rmt_channel_handle_t ch, const rmt_rx_done_event_data_t *edata, void *user_data)
{
    (void)ch;
    QueueHandle_t q = (QueueHandle_t)user_data;
    rx_done_msg_t m = { .num_symbols = edata->num_symbols };
    BaseType_t hp = pdFALSE;
    xQueueSendFromISR(q, &m, &hp);
    return (hp == pdTRUE);
}

static void ir_init_once(void)
{
    rmt_tx_channel_config_t tx_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = IR_TX_GPIO,
        .mem_block_symbols = 64,
        .resolution_hz = RMT_RES_HZ,
        .trans_queue_depth = 4,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_cfg, &s_tx_chan));

    
    ESP_LOGI(TAG, "modulate carrier to TX channel");
    rmt_carrier_config_t carrier_cfg = {
        .duty_cycle = 0.33,
        .frequency_hz = 38000, // 38KHz
    };
    ESP_ERROR_CHECK(rmt_apply_carrier(s_tx_chan, &carrier_cfg));


    ir_nec_encoder_config_t nec_cfg = { .resolution = RMT_RES_HZ };
    ESP_ERROR_CHECK(rmt_new_ir_nec_encoder(&nec_cfg, &s_nec_encoder));
    ESP_ERROR_CHECK(rmt_enable(s_tx_chan));

    rmt_rx_channel_config_t rx_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = IR_RX_GPIO,
        .mem_block_symbols = 64,
        .resolution_hz = RMT_RES_HZ,
    };
    ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_cfg, &s_rx_chan));
    ESP_ERROR_CHECK(rmt_enable(s_rx_chan));

    s_rx_done_q = xQueueCreate(4, sizeof(rx_done_msg_t));
    rmt_rx_event_callbacks_t cbs = { .on_recv_done = on_rx_done };
    ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(s_rx_chan, &cbs, s_rx_done_q));
}

static esp_err_t ensure_rx_enabled(void)
{
    // rmt_enable() is safe to call if already enabled (returns OK)
    esp_err_t err = rmt_enable(s_rx_chan);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "rmt_enable(rx) failed: %s", esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}

static void post_ir_event(app_evt_type_t type, uint32_t request_id, const ir_nec_code_t *code)
{
    app_evt_t event = {.type = type, .request_id = request_id};
    if (code) event.ir.code = *code;
    app_post_evt(&event);
}

static esp_err_t reset_receiver(void)
{
    esp_err_t err = rmt_disable(s_rx_chan);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    xQueueReset(s_rx_done_q);
    return ensure_rx_enabled();
}

static void ir_worker_task(void *arg)
{
    (void)arg;
    app_ctx_t *app = app_get_ctx();
    rmt_receive_config_t config = {
        .signal_range_min_ns = 1250,
        .signal_range_max_ns = 12000000,
    };
    bool learning = false;
    uint32_t learn_id = 0;
    int64_t deadline = 0;

    for (;;) {
        app_cmd_t command;
        // Poll during capture so BACK/HOME can cancel immediately.
        if (xQueueReceive(app->cmd_q, &command, learning ? pdMS_TO_TICKS(20) : portMAX_DELAY) == pdTRUE) {
            if (command.type == APP_CMD_IR_LEARN_CANCEL) {
                if (learning && command.request_id == learn_id) {
                    learning = false;
                    (void)reset_receiver();
                }
            } else if (command.type == APP_CMD_IR_LEARN_ONESHOT) {
                learning = false;
                esp_err_t err = reset_receiver();
                if (err == ESP_OK)
                    err = rmt_receive(s_rx_chan, s_rx_buf, sizeof(s_rx_buf), &config);
                if (err == ESP_OK) {
                    learning = true;
                    learn_id = command.request_id;
                    deadline = esp_timer_get_time() + (int64_t)command.learn.timeout_ms * 1000;
                } else {
                    post_ir_event(APP_EVT_IR_LEARN_FAILED, command.request_id, NULL);
                }
            } else if (command.type == APP_CMD_IR_TX_SEND) {
                ir_nec_scan_code_t scan = {
                    .address = command.tx.code.addr,
                    .command = command.tx.code.cmd,
                };
                rmt_transmit_config_t tx_config = {.loop_count = 0};
                esp_err_t err = rmt_transmit(s_tx_chan, s_nec_encoder, &scan, sizeof(scan), &tx_config);
                // This API accepts milliseconds, not FreeRTOS ticks.
                if (err == ESP_OK) err = rmt_tx_wait_all_done(s_tx_chan, 500);
                if (err != ESP_OK) {
                    (void)rmt_disable(s_tx_chan);
                    (void)rmt_enable(s_tx_chan);
                }
                post_ir_event(err == ESP_OK ? APP_EVT_IR_TX_DONE : APP_EVT_IR_TX_FAIL,
                              command.request_id, NULL);
            }
        }
        if (!learning) continue;
        rx_done_msg_t done;
        if (xQueueReceive(s_rx_done_q, &done, 0) == pdTRUE) {
            ir_nec_code_t code;
            debug_print_raw_symbols(s_rx_buf, done.num_symbols);
            if (nec_try_decode(s_rx_buf, done.num_symbols, &code)) {
                learning = false;
                post_ir_event(APP_EVT_IR_LEARNED, learn_id, &code);
                ESP_LOGI(TAG, "Learned NEC addr=%04X cmd=%04X", code.addr, code.cmd);
            } else {
                // Keep listening after noise/unsupported frames within the original deadline.
                post_ir_event(APP_EVT_IR_UNSUPPORTED, learn_id, NULL);
                if (rmt_receive(s_rx_chan, s_rx_buf, sizeof(s_rx_buf), &config) != ESP_OK) {
                    learning = false;
                    (void)reset_receiver();
                    post_ir_event(APP_EVT_IR_LEARN_FAILED, learn_id, NULL);
                }
            }
        }
        if (learning && esp_timer_get_time() >= deadline) {
            learning = false;
            (void)reset_receiver();
            post_ir_event(APP_EVT_IR_LEARN_TIMEOUT, learn_id, NULL);
        }
    }
}

void ir_start_tasks(void)
{
    ir_init_once();
    ESP_ERROR_CHECK(xTaskCreate(ir_worker_task, "ir_worker", 4096, NULL, 6, NULL) == pdPASS
        ? ESP_OK : ESP_ERR_NO_MEM);
}
