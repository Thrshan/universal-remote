#include "bt_keyboard.h"
#include "bt_advertising.h"
#include "bt_output.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_hidd.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#define LINK_DOWN BIT0
#define BOND_REMOVED BIT1
#define ADV_STOPPED BIT2
#define PAIR_WINDOW_MS 120000

typedef enum {
    MSG_OPEN, MSG_CLOSE, MSG_FORGET, MSG_SAVED, MSG_TAP, MSG_RELEASE,
    MSG_HID_READY, MSG_ADV_DATA, MSG_SCAN_DATA, MSG_WHITELIST, MSG_ADDRESS, MSG_ADV_STARTED,
    MSG_CONNECTED, MSG_AUTH, MSG_DISCONNECTED, MSG_WRITE, MSG_SUSPEND,
} message_type_t;
typedef struct {
    message_type_t type;
    uint32_t session, generation, request;
    remote_t remote;
    unsigned key, profile_index;
    bool flag;
    esp_err_t error;
    uint8_t peer[6], addr_type;
} message_t;

static const char *TAG = "bt_keyboard";
static QueueHandle_t s_queue;
static EventGroupHandle_t s_events;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bt_snapshot_t s_snapshot;
static uint32_t s_desired_session, s_callback_generation;
static uint32_t s_input_generation;
static esp_err_t s_remove_result;
static const char *s_operation = "Bluetooth";

// Only the worker owns the stack, reports, and connection state.
static struct {
    uint32_t session, generation;
    remote_t profile;
    unsigned profile_index;
    esp_hidd_dev_t *hid;
    bool controller_init, controller_on, host_init, host_on;
    bool hid_ready, adv_ready, scan_ready, whitelist_ready, address_ready, advertising;
    bool connected, encrypted, subscribed, saved, suspended;
    bool pairing, awaiting_save, forgetting, pair_after;
    uint8_t peer[6];
    int64_t pair_deadline, connect_deadline, save_deadline;
    char name[24];
} s;

static uint32_t desired_session(void)
{
    portENTER_CRITICAL(&s_lock);
    uint32_t result = s_desired_session;
    portEXIT_CRITICAL(&s_lock);
    return result;
}
static void status(bt_status_t state, esp_err_t error)
{
    portENTER_CRITICAL(&s_lock);
    bool changed = s_snapshot.session != s.session || s_snapshot.state != state || s_snapshot.error != error;
    s_snapshot = (bt_snapshot_t){.session = s.session, .state = state, .error = error, .step = s_operation};
    portEXIT_CRITICAL(&s_lock);
    if (changed) ESP_LOGI(TAG, "Session %u: %s", (unsigned)s.session, bt_keyboard_status_name(state));
}
bt_snapshot_t bt_keyboard_snapshot(void)
{
    portENTER_CRITICAL(&s_lock);
    bt_snapshot_t result = s_snapshot;
    portEXIT_CRITICAL(&s_lock);
    return result;
}
static bool enqueue(message_t *message)
{ return s_queue && xQueueSend(s_queue, message, 0) == pdTRUE; }
static void callback_message(message_t message)
{
    portENTER_CRITICAL(&s_lock);
    message.generation = s_callback_generation;
    portEXIT_CRITICAL(&s_lock);
    if (message.generation && !enqueue(&message)) ESP_LOGE(TAG, "Bluetooth event queue full");
}
static void gap_callback(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    message_t msg = {0};
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
            msg.type = MSG_ADV_DATA; msg.error = param->adv_data_cmpl.status; break;
        case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
            msg.type = MSG_SCAN_DATA; msg.error = param->scan_rsp_data_cmpl.status; break;
        case ESP_GAP_BLE_UPDATE_WHITELIST_COMPLETE_EVT:
            msg.type = MSG_WHITELIST;
            msg.error = param->update_whitelist_cmpl.status;
            msg.key = param->update_whitelist_cmpl.wl_operation;
            break;
        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            msg.type = MSG_ADV_STARTED; msg.error = param->adv_start_cmpl.status; break;
        case ESP_GAP_BLE_SET_STATIC_RAND_ADDR_EVT:
            msg.type = MSG_ADDRESS; msg.error = param->set_rand_addr_cmpl.status; break;
        case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
            xEventGroupSetBits(s_events, ADV_STOPPED); return;
        case ESP_GAP_BLE_REMOVE_BOND_DEV_COMPLETE_EVT:
            s_remove_result = param->remove_bond_dev_cmpl.status == ESP_BT_STATUS_SUCCESS ? ESP_OK : ESP_FAIL;
            xEventGroupSetBits(s_events, BOND_REMOVED); return;
        case ESP_GAP_BLE_SEC_REQ_EVT:
            esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, desired_session() != 0);
            return;
        case ESP_GAP_BLE_AUTH_CMPL_EVT:
            msg.type = MSG_AUTH;
            msg.error = param->ble_security.auth_cmpl.success ? ESP_OK : ESP_FAIL;
            if (msg.error != ESP_OK)
                ESP_LOGW(TAG, "Pairing rejected, reason 0x%02x", param->ble_security.auth_cmpl.fail_reason);
            memcpy(msg.peer, param->ble_security.auth_cmpl.bd_addr, 6);
            msg.addr_type = param->ble_security.auth_cmpl.addr_type;
            break;
        default: return;
    }
    callback_message(msg);
}
static void gatts_callback(esp_gatts_cb_event_t event, esp_gatt_if_t interface,
                           esp_ble_gatts_cb_param_t *param)
{
    // Let esp_hid process report subscriptions and release its send semaphore
    // before asking the worker to send anything.
    esp_hidd_gatts_event_handler(event, interface, param);
    if (event == ESP_GATTS_CONNECT_EVT) {
        message_t msg = {.type = MSG_CONNECTED};
        memcpy(msg.peer, param->connect.remote_bda, 6);
        callback_message(msg);
    } else if (event == ESP_GATTS_DISCONNECT_EVT) {
        xEventGroupSetBits(s_events, LINK_DOWN);
        callback_message((message_t){.type = MSG_DISCONNECTED});
    } else if (event == ESP_GATTS_WRITE_EVT) {
        callback_message((message_t){.type = MSG_WRITE});
    }
}
static void hid_callback(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == ESP_HIDD_START_EVENT) callback_message((message_t){.type = MSG_HID_READY});
    else if (id == ESP_HIDD_CONTROL_EVENT) {
        esp_hidd_event_data_t *event = data;
        callback_message((message_t){.type = MSG_SUSPEND, .flag = !event->control.control});
    }
}
static bool bond_exists(const uint8_t *peer)
{
    int count = esp_ble_get_bond_device_num();
    if (count <= 0) return false;
    esp_ble_bond_dev_t *bonds = calloc(count, sizeof(*bonds));
    if (!bonds) return false;
    bool found = false;
    if (esp_ble_get_bond_device_list(&count, bonds) == ESP_OK)
        for (int i = 0; i < count; ++i)
            if (memcmp(peer, bonds[i].bd_addr, 6) == 0) found = true;
    free(bonds);
    return found;
}
static bool bond_used_elsewhere(const uint8_t *peer)
{
    remote_t remote;
    for (unsigned i = 0; i < app_remote_count(); ++i)
        if (i != s.profile_index && app_remote_get(i, &remote) && remote.transport == REMOTE_BT
            && remote.bt.bonded && memcmp(remote.bt.peer, peer, 6) == 0) return true;
    return false;
}
static void release_reports(void)
{
    if (!s.hid || !s.connected || !s.encrypted || !s.subscribed) return;
    uint8_t zero[8] = {0};
    (void)esp_hidd_dev_input_set(s.hid, 0, 1, zero, 8);
    (void)esp_hidd_dev_input_set(s.hid, 0, 2, zero, 2);
}
static void disconnect_link(void)
{
    if (!s.connected) return;
    release_reports();
    xEventGroupClearBits(s_events, LINK_DOWN);
    if (esp_ble_gap_disconnect(s.peer) == ESP_OK)
        (void)xEventGroupWaitBits(s_events, LINK_DOWN, pdTRUE, pdTRUE, pdMS_TO_TICKS(1500));
    s.connected = s.encrypted = s.subscribed = false;
}
static void stop_advertising(void)
{
    if (!s.advertising) return;
    xEventGroupClearBits(s_events, ADV_STOPPED);
    if (esp_ble_gap_stop_advertising() == ESP_OK)
        (void)xEventGroupWaitBits(s_events, ADV_STOPPED, pdTRUE, pdTRUE, pdMS_TO_TICKS(500));
    s.advertising = false;
}
static void stop_runtime(void)
{
    if (s.controller_on || s.host_on) status(BT_STOPPING, ESP_OK);
    stop_advertising();
    disconnect_link();
    if (s.hid) {
        esp_hidd_dev_event_handler_unregister(s.hid, hid_callback, ESP_HIDD_ANY_EVENT);
        esp_hidd_dev_deinit(s.hid);
        s.hid = NULL;
    }
    if (s.host_on) esp_bluedroid_disable();
    if (s.host_init) esp_bluedroid_deinit();
    if (s.controller_on) esp_bt_controller_disable();
    // Controller deinit is a final, once-only operation. Disable its radio,
    // retaining controller initialization. Bluedroid must be deinitialized:
    // IDF 5.5.2 frees privacy-list buffers on disable and does not recreate
    // them on enable alone (btm_ble_enq_resolving_list_pending can crash).
    portENTER_CRITICAL(&s_lock);
    s_callback_generation = 0;
    portEXIT_CRITICAL(&s_lock);
    s.controller_on = s.host_on = s.host_init = false;
    s.hid_ready = s.adv_ready = s.scan_ready = s.whitelist_ready = false;
    s.address_ready = false;
    s.pair_deadline = s.connect_deadline = s.save_deadline = 0;
    s.awaiting_save = s.forgetting = false;
}
static void fail(esp_err_t error)
{
    ESP_LOGE(TAG, "%s failed: %s (0x%x)", s_operation, esp_err_to_name(error), (unsigned)error);
    stop_runtime();
    status(BT_ERROR, error);
}
static esp_err_t start_runtime(void)
{
    status(BT_STARTING, ESP_OK);
    ++s.generation;
    portENTER_CRITICAL(&s_lock);
    s_callback_generation = s.generation;
    portEXIT_CRITICAL(&s_lock);
    s_operation = "Controller";
    esp_err_t err;
    if (!s.controller_init) {
        esp_bt_controller_config_t controller = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        if ((err = esp_bt_controller_init(&controller)) != ESP_OK) return err;
        s.controller_init = true;
    }
    if ((err = esp_bt_controller_enable(ESP_BT_MODE_BLE)) != ESP_OK) return err;
    s.controller_on = true;
    s_operation = "BT host";
    if (!s.host_init) {
        if ((err = esp_bluedroid_init()) != ESP_OK) return err;
        s.host_init = true;
    }
    if ((err = esp_bluedroid_enable()) != ESP_OK) return err;
    s.host_on = true;
    s_operation = "Callbacks";
    if ((err = esp_ble_gap_register_callback(gap_callback)) != ESP_OK) return err;
    if ((err = esp_ble_gatts_register_callback(gatts_callback)) != ESP_OK) return err;
    s.address_ready = !remote_bt_has_identity(&s.profile);
    if (!s.address_ready) {
        s_operation = "BT identity";
        if ((err = esp_ble_gap_set_rand_addr(s.profile.bt.local_addr)) != ESP_OK) return err;
    }

    // Headless keyboard: encrypted bonding, without pretending to have a
    // display/confirmation key or requiring a hard-coded demo passkey.
    s_operation = "Security";
    esp_ble_auth_req_t auth = ESP_LE_AUTH_BOND;
    esp_ble_io_cap_t io = ESP_IO_CAP_NONE;
    uint8_t keys = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK, key_size = 16;
    if ((err = esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth, sizeof(auth))) != ESP_OK) return err;
    if ((err = esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &io, sizeof(io))) != ESP_OK) return err;
    if ((err = esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, 1)) != ESP_OK) return err;
    if ((err = esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &keys, 1)) != ESP_OK) return err;
    if ((err = esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &keys, 1)) != ESP_OK) return err;

    s.pairing = !s.profile.bt.bonded || !bond_exists(s.profile.bt.peer);
    s.saved = !s.pairing;
    s.awaiting_save = s.forgetting = s.suspended = false;
    // A disabled controller can retain filtering state. Clear first, then
    // asynchronously add only this profile's saved peer before advertising.
    s.whitelist_ready = false;
    s_operation = "Clear peers";
    if ((err = esp_ble_gap_clear_whitelist()) != ESP_OK) return err;
    snprintf(s.name, sizeof(s.name), "UR %.12s", s.profile.name);
    s_operation = "Device name";
    if ((err = esp_ble_gap_set_device_name(s.name)) != ESP_OK) return err;
    s_operation = "Advertising";
    if ((err = bt_configure_advertising(ESP_HID_APPEARANCE_KEYBOARD)) != ESP_OK) return err;
    esp_hid_raw_report_map_t map = {.data = bt_keyboard_report_map, .len = bt_keyboard_report_map_size};
    esp_hid_device_config_t config = {
        .vendor_id = 0x303A, .product_id = 0x4001, .version = 0x0100,
        .device_name = s.name, .manufacturer_name = "Universal Remote",
        .serial_number = "URKeyboard", .report_maps = &map, .report_maps_len = 1,
    };
    s_operation = "HID service";
    return esp_hidd_dev_init(&config, ESP_HID_TRANSPORT_BLE, hid_callback, &s.hid);
}
static void advertise_if_ready(void)
{
    if (!s.hid_ready || !s.adv_ready || !s.scan_ready || !s.whitelist_ready || !s.address_ready
        || s.connected || s.advertising || s.awaiting_save
        || desired_session() != s.session) return;
    esp_ble_adv_params_t params = {
        .adv_int_min = 0x30, .adv_int_max = 0x50, .adv_type = ADV_TYPE_IND,
        .own_addr_type = remote_bt_has_identity(&s.profile) ? BLE_ADDR_TYPE_RANDOM : BLE_ADDR_TYPE_PUBLIC,
        .channel_map = ADV_CHNL_ALL,
        .adv_filter_policy = s.pairing ? ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY : ADV_FILTER_ALLOW_SCAN_ANY_CON_WLST,
    };
    esp_err_t err = esp_ble_gap_start_advertising(&params);
    s_operation = "Advertising";
    if (err != ESP_OK) { fail(err); return; }
    s.advertising = true;
    if (s.pairing && !s.pair_deadline) s.pair_deadline = esp_timer_get_time() + PAIR_WINDOW_MS * 1000LL;
}
static void ready_if_subscribed(void)
{
    if (!s.connected || !s.encrypted || !s.saved || s.awaiting_save) return;
    if (!s.subscribed) {
        uint8_t zero[8] = {0};
        s.subscribed = esp_hidd_dev_input_set(s.hid, 0, 1, zero, 8) == ESP_OK;
    }
    if (s.subscribed) {
        s.connect_deadline = s.pair_deadline = 0;
        status(s.suspended ? BT_SUSPENDED : BT_CONNECTED, ESP_OK);
    }
}
static void post_peer(bool bonded, uint8_t type, const uint8_t *peer)
{
    app_evt_t event = {.type = APP_EVT_BT_PEER, .request_id = s.session};
    event.bt.bonded = bonded;
    event.bt.addr_type = type;
    if (peer) memcpy(event.bt.peer, peer, 6);
    s.awaiting_save = true;
    s.save_deadline = esp_timer_get_time() + 10000000LL;
    app_post_evt(&event);
}
typedef struct { const message_t *message; bool failed; } output_context_t;
static bool output_active(void *context)
{
    const message_t *msg = ((output_context_t *)context)->message;
    portENTER_CRITICAL(&s_lock);
    bool active = s_desired_session == msg->session && s_input_generation == msg->generation;
    portEXIT_CRITICAL(&s_lock);
    return active && s.connected && s.encrypted && s.saved && s.subscribed;
}
static bool output_send(void *context, const bt_key_report_t *report)
{
    bool sent = esp_hidd_dev_input_set(s.hid, 0, report->report_id, (uint8_t *)report->data, report->length) == ESP_OK;
    if (!sent) ((output_context_t *)context)->failed = true;
    return sent;
}
static void output_pause(void *context, unsigned milliseconds)
{ vTaskDelay(pdMS_TO_TICKS(milliseconds)); }
static void tap(const message_t *msg)
{
    app_evt_t result = {.type = APP_EVT_BT_TX_FAIL, .request_id = msg->request};
    remote_t remote;
    output_context_t context = {.message = msg};
    if (output_active(&context) && app_remote_get(s.profile_index, &remote)) {
        bt_output_t output = {.context = &context, .active = output_active,
            .send = output_send, .pause = output_pause};
        // ESP-IDF advertises HID remote-wake support. An explicit user press
        // may wake a suspended host; periodic/background input is never sent.
        bool sent = false;
        if (remote.bt.preset == BT_PRESET_TEXT && !msg->flag) {
            char text[BT_TEXT_MAX + 1];
            if (app_remote_get_bt_text(s.profile_index, msg->key, text)) sent = bt_output_text(&output, text);
        } else {
            bt_key_report_t report;
            if (bt_profile_report(&remote, msg->key, msg->flag, &report)) sent = bt_output_tap(&output, &report);
        }
        if (sent) result.type = APP_EVT_BT_TX_DONE;
        else if (context.failed && desired_session() == s.session) {
            // Never leave a key or modifier held after a failed release.
            stop_runtime();
            esp_err_t restarted = start_runtime();
            if (restarted != ESP_OK) fail(restarted);
        }
    }
    app_post_evt(&result);
}
static void worker(void *arg)
{
    for (;;) {
        message_t msg;
        if (xQueueReceive(s_queue, &msg, pdMS_TO_TICKS(25)) == pdTRUE) {
            if (msg.type == MSG_OPEN) {
                if (msg.session != desired_session()) continue;
                stop_runtime();
                s.session = msg.session;
                s.profile = msg.remote;
                s.profile_index = msg.profile_index;
                esp_err_t err = start_runtime();
                if (err != ESP_OK) fail(err);
            } else if (msg.type == MSG_CLOSE) {
                // The desired-session check below also handles a full queue.
            } else if (msg.type == MSG_FORGET && msg.session == s.session && msg.session == desired_session()) {
                stop_runtime();
                // Initialize without advertising, remove the stored peer, then
                // wait for the UI to commit the cleared profile before pairing.
                remote_t old = s.profile;
                s.profile.bt.bonded = 0;
                esp_err_t err = start_runtime();
                s.profile = old;
                if (err == ESP_OK && old.bt.bonded && !bond_used_elsewhere(old.bt.peer) && bond_exists(old.bt.peer)) {
                    xEventGroupClearBits(s_events, BOND_REMOVED);
                    s_remove_result = ESP_FAIL;
                    err = esp_ble_remove_bond_device(old.bt.peer);
                    if (err == ESP_OK) {
                        EventBits_t bits = xEventGroupWaitBits(s_events, BOND_REMOVED, pdTRUE, pdTRUE, pdMS_TO_TICKS(1500));
                        err = (bits & BOND_REMOVED) ? s_remove_result : ESP_ERR_TIMEOUT;
                    }
                }
                if (err != ESP_OK) fail(err);
                else {
                    s.forgetting = true; s.pair_after = msg.flag;
                    post_peer(false, 0, NULL);
                }
            } else if (msg.type == MSG_SAVED && msg.session == s.session && s.awaiting_save) {
                s.awaiting_save = false;
                s.save_deadline = 0;
                if (!msg.flag) { fail(ESP_FAIL); continue; }
                if (s.forgetting) {
                    s.profile = msg.remote;
                    s.forgetting = false;
                    if (!s.pair_after) { stop_runtime(); status(BT_UNPAIRED, ESP_OK); }
                    else {
                        // Restart with the new, durably saved keyboard identity.
                        // Windows must discover a fresh accessory after Unpair.
                        stop_runtime();
                        esp_err_t err = start_runtime();
                        if (err != ESP_OK) fail(err);
                    }
                } else {
                    s.saved = true;
                    s.pairing = false;
                    // The host may disappear between authentication and the
                    // UI's durable-save acknowledgment. Resume its reconnect
                    // path rather than waiting forever without advertising.
                    if (!s.connected) {
                        stop_runtime();
                        esp_err_t err = start_runtime();
                        if (err != ESP_OK) fail(err);
                    } else ready_if_subscribed();
                }
            } else if (msg.type == MSG_TAP && msg.session == s.session) tap(&msg);
            else if (msg.type == MSG_RELEASE && msg.session == s.session) release_reports();
            else if (msg.generation && msg.generation == s.generation && s.host_on) {
                if (desired_session() != s.session) continue;
                switch (msg.type) {
                    case MSG_HID_READY: s.hid_ready = true; break;
                    case MSG_ADV_DATA: s_operation = "Adv payload"; s.adv_ready = msg.error == ESP_OK; break;
                    case MSG_SCAN_DATA: s_operation = "Scan response"; s.scan_ready = msg.error == ESP_OK; break;
                    case MSG_ADDRESS: s_operation = "BT identity"; s.address_ready = msg.error == ESP_OK; break;
                    case MSG_WHITELIST:
                        s_operation = "Saved peer";
                        if (msg.error == ESP_OK && msg.key == ESP_BLE_WHITELIST_CLEAR && !s.pairing) {
                            esp_ble_wl_addr_type_t type = (s.profile.bt.addr_type & 1) ? BLE_WL_ADDR_TYPE_RANDOM : BLE_WL_ADDR_TYPE_PUBLIC;
                            esp_err_t err = esp_ble_gap_update_whitelist(true, s.profile.bt.peer, type);
                            if (err != ESP_OK) fail(err);
                        } else s.whitelist_ready = msg.error == ESP_OK;
                        break;
                    case MSG_ADV_STARTED:
                        if (msg.error == ESP_OK) status(s.pairing ? BT_PAIRING : BT_RECONNECTING, ESP_OK);
                        break;
                    case MSG_CONNECTED:
                        if (!s.connected) {
                            memcpy(s.peer, msg.peer, 6);
                            s.connected = true; s.advertising = false;
                            s.connect_deadline = esp_timer_get_time() + 30000000LL;
                            status(BT_CONNECTING, ESP_OK);
                        }
                        break;
                    case MSG_AUTH:
                        s_operation = "Pairing";
                        if (msg.error != ESP_OK) {
                            if (!s.pairing) { fail(msg.error); break; }
                            // A stale-key or cancelled host attempt must not
                            // close the whole fresh-pairing window. Disconnect
                            // that link and keep advertising until its deadline.
                            s.encrypted = s.subscribed = false;
                            disconnect_link();
                            status(BT_PAIRING, msg.error);
                            msg.error = ESP_OK; // This failure is recoverable.
                            break;
                        }
                        s.encrypted = true;
                        if (s.pairing) {
                            s.profile.bt.bonded = 1;
                            s.profile.bt.addr_type = msg.addr_type;
                            memcpy(s.profile.bt.peer, msg.peer, 6);
                            post_peer(true, msg.addr_type, msg.peer);
                        } else ready_if_subscribed();
                        break;
                    case MSG_DISCONNECTED:
                        if (s.connected) {
                            s.connected = s.encrypted = s.subscribed = s.suspended = false;
                            s.connect_deadline = 0;
                            // Once paired, reconnect only to this profile's host.
                            if (s.saved && s.profile.bt.bonded) {
                                stop_runtime();
                                esp_err_t err = start_runtime();
                                if (err != ESP_OK) fail(err);
                            } else status(BT_PAIRING, ESP_OK);
                        }
                        break;
                    case MSG_WRITE: ready_if_subscribed(); break;
                    case MSG_SUSPEND:
                        if (msg.flag) release_reports();
                        s.suspended = msg.flag;
                        ready_if_subscribed();
                        break;
                    default: break;
                }
                if (msg.error != ESP_OK && s.host_on) fail(msg.error);
                else advertise_if_ready();
            }
        }
        if (!desired_session() && s.session) {
            stop_runtime(); status(BT_OFF, ESP_OK); s.session = 0;
        }
        int64_t now = esp_timer_get_time();
        if (s.host_on && s.save_deadline && now >= s.save_deadline) {
            fail(ESP_ERR_TIMEOUT);
        }
        if (s.host_on && ((s.pair_deadline && now >= s.pair_deadline)
            || (s.connect_deadline && now >= s.connect_deadline))) {
            stop_runtime(); status(BT_PAIR_TIMEOUT, ESP_ERR_TIMEOUT);
        }
    }
}
void bt_keyboard_init(void)
{
    s_queue = xQueueCreate(32, sizeof(message_t));
    s_events = xEventGroupCreate();
    ESP_ERROR_CHECK(s_queue && s_events ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(worker, "bt_keyboard", 6144, NULL, 5, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
bool bt_keyboard_open(const remote_t *remote, unsigned profile_index, uint32_t session)
{
    if (!remote || remote->transport != REMOTE_BT || remote->bt.kind != BT_REMOTE_KEYBOARD || !session) return false;
    message_t msg = {.type = MSG_OPEN, .session = session, .remote = *remote, .profile_index = profile_index};
    portENTER_CRITICAL(&s_lock);
    uint32_t previous = s_desired_session;
    s_desired_session = session;
    portEXIT_CRITICAL(&s_lock);
    if (enqueue(&msg)) return true;
    portENTER_CRITICAL(&s_lock);
    s_desired_session = previous;
    portEXIT_CRITICAL(&s_lock);
    return false;
}
void bt_keyboard_close(void)
{
    portENTER_CRITICAL(&s_lock);
    s_desired_session = 0;
    portEXIT_CRITICAL(&s_lock);
    message_t msg = {.type = MSG_CLOSE};
    (void)enqueue(&msg);
}
bool bt_keyboard_forget(uint32_t session, bool pair_after)
{
    message_t msg = {.type = MSG_FORGET, .session = session, .flag = pair_after};
    return enqueue(&msg);
}
void bt_keyboard_peer_saved(uint32_t session, bool success, const remote_t *remote)
{
    message_t msg = {.type = MSG_SAVED, .session = session, .flag = success};
    if (remote) msg.remote = *remote;
    if (!enqueue(&msg)) bt_keyboard_close();
}
bool bt_keyboard_tap(uint32_t session, uint32_t request, unsigned key, bool long_hold)
{
    bt_snapshot_t snapshot = bt_keyboard_snapshot();
    if (snapshot.session != session || (snapshot.state != BT_CONNECTED && snapshot.state != BT_SUSPENDED)
        || desired_session() != session) return false;
    message_t msg = {.type = MSG_TAP, .session = session, .request = request, .key = key, .flag = long_hold};
    portENTER_CRITICAL(&s_lock);
    msg.generation = s_input_generation;
    portEXIT_CRITICAL(&s_lock);
    return enqueue(&msg);
}
void bt_keyboard_release(uint32_t session)
{
    portENTER_CRITICAL(&s_lock);
    ++s_input_generation;
    portEXIT_CRITICAL(&s_lock);
    message_t msg = {.type = MSG_RELEASE, .session = session};
    (void)enqueue(&msg);
}
