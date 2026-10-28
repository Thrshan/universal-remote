#include "bt_keyboard.h"
#include "bt_advertising.h"
#include "bt_output.h"
#include "bt_ccc_store.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
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
    MSG_OPEN, MSG_CLOSE, MSG_FORGET, MSG_SAVED, MSG_TAP, MSG_RELEASE, MSG_MOUSE,
    MSG_HID_READY, MSG_ADV_DATA, MSG_SCAN_DATA, MSG_WHITELIST, MSG_ADDRESS, MSG_ADV_STARTED,
    MSG_PEER_PRIVACY, MSG_PRIVACY, MSG_CONNECTED, MSG_AUTH, MSG_DISCONNECTED, MSG_WRITE, MSG_SUSPEND,
} message_type_t;
typedef enum { PRIVACY_RESETTING, PRIVACY_PREPARING, PRIVACY_ENABLING, PRIVACY_READY } privacy_phase_t;

typedef struct {
    message_type_t type;
    uint32_t session, generation, request;
    remote_t remote;
    unsigned key, profile_index;
    bt_mouse_input_t mouse_input;
    uint32_t time_ms;
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
// Callback-level link state survives invalidation of queued worker messages.
// In particular, CLOSE can overtake the worker's CONNECT message.
static struct { bool up; uint8_t peer[6]; } s_link;

// Only the worker owns the stack, reports, and connection state.
static struct {
    uint32_t session, generation;
    remote_t profile;
    unsigned profile_index;
    esp_hidd_dev_t *hid;
    bool controller_init, controller_on, host_init, host_on;
    bool hid_ready, adv_ready, scan_ready, whitelist_ready, address_ready, advertising;
    bool privacy_ready, peer_has_irk;
    privacy_phase_t privacy_phase;
    bool connected, encrypted, subscribed, saved, suspended;
    bool pairing, awaiting_save, forgetting, pair_after;
    uint8_t peer[6];
    uint8_t peer_identity[6];
    esp_ble_addr_type_t peer_identity_type;
    int64_t start_deadline, pair_deadline, connect_deadline, save_deadline;
    char name[24], store_name[16];
    esp_hidd_ccc_t persisted_ccc;
    bool ccc_persisted;
    bt_mouse_t mouse;
    uint32_t mouse_generation;
} s;

static uint32_t desired_session(void)
{
    portENTER_CRITICAL(&s_lock);
    uint32_t result = s_desired_session;
    portEXIT_CRITICAL(&s_lock);
    return result;
}
static bool physical_link(uint8_t *peer)
{
    portENTER_CRITICAL(&s_lock);
    bool up = s_link.up;
    if (up && peer) memcpy(peer, s_link.peer, 6);
    portEXIT_CRITICAL(&s_lock);
    return up;
}
static void lifecycle_step(const char *step)
{
    s_operation = step;
    portENTER_CRITICAL(&s_lock);
    if (s_snapshot.session == s.session) s_snapshot.step = step;
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGI(TAG, "Session %u: %s", (unsigned)s.session, step);
}
static void status(bt_status_t state, esp_err_t error)
{
    portENTER_CRITICAL(&s_lock);
    bool changed = s_snapshot.session != s.session || s_snapshot.state != state || s_snapshot.error != error;
    s_snapshot = (bt_snapshot_t){.session = s.session, .state = state, .error = error, .step = s_operation,
        .mouse_movement = s.mouse.movement, .mouse_buttons = s.mouse.buttons};
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
        case ESP_GAP_BLE_SET_PRIVACY_MODE_COMPLETE_EVT:
            msg.type = MSG_PEER_PRIVACY; msg.error = param->set_privacy_mode_cmpl.status; break;
        case ESP_GAP_BLE_SET_LOCAL_PRIVACY_COMPLETE_EVT:
            msg.type = MSG_PRIVACY; msg.error = param->local_privacy_cmpl.status; break;
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
    if (event == ESP_GATTS_CONNECT_EVT) {
        xEventGroupClearBits(s_events, LINK_DOWN);
        portENTER_CRITICAL(&s_lock);
        s_link.up = true;
        memcpy(s_link.peer, param->connect.remote_bda, 6);
        portEXIT_CRITICAL(&s_lock);
    } else if (event == ESP_GATTS_DISCONNECT_EVT) {
        portENTER_CRITICAL(&s_lock);
        s_link.up = false;
        portEXIT_CRITICAL(&s_lock);
    }
    // Let esp_hid process report subscriptions and release its send semaphore
    // before asking the worker to send anything.
    esp_hidd_gatts_event_handler(event, interface, param);
    if (event == ESP_GATTS_CONNECT_EVT) {
        message_t msg = {.type = MSG_CONNECTED};
        memcpy(msg.peer, param->connect.remote_bda, 6);
        callback_message(msg);
    } else if (event == ESP_GATTS_DISCONNECT_EVT) {
        ESP_LOGI(TAG, "GATT disconnect reason 0x%02x", param->disconnect.reason);
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
static bool bond_matches(const esp_ble_bond_dev_t *bond, const uint8_t *peer, uint8_t addr_type)
{
    const esp_ble_pid_keys_t *pid = &bond->bond_key.pid_key;
    // Profiles can contain the bond database address or resolved identity.
    bool database_match = bond->bd_addr_type <= BLE_ADDR_TYPE_RPA_RANDOM
        && (bond->bd_addr_type & 1) == (addr_type & 1) && memcmp(peer, bond->bd_addr, 6) == 0;
    bool identity_match = (bond->bond_key.key_mask & ESP_BLE_ID_KEY_MASK)
        && pid->addr_type <= BLE_ADDR_TYPE_RANDOM && pid->addr_type == (addr_type & 1)
        && memcmp(peer, pid->static_addr, 6) == 0;
    return addr_type <= BLE_ADDR_TYPE_RPA_RANDOM && (database_match || identity_match);
}
static esp_err_t find_bond(const uint8_t *peer, uint8_t addr_type, esp_ble_bond_dev_t *bond)
{
    int count = esp_ble_get_bond_device_num();
    if (count < 0) return ESP_FAIL;
    if (!count) return ESP_ERR_NOT_FOUND;
    esp_ble_bond_dev_t *bonds = calloc(count, sizeof(*bonds));
    if (!bonds) return ESP_ERR_NO_MEM;
    esp_err_t err = esp_ble_get_bond_device_list(&count, bonds);
    if (err == ESP_OK) {
        err = ESP_ERR_NOT_FOUND;
        for (int i = 0; i < count; ++i) {
            if (bond_matches(&bonds[i], peer, addr_type)) {
                *bond = bonds[i];
                err = ESP_OK;
                break;
            }
        }
    }
    free(bonds);
    return err;
}
static esp_err_t bond_identity(const esp_ble_bond_dev_t *bond, uint8_t *identity,
                               esp_ble_addr_type_t *type, bool *has_irk)
{
    static const uint8_t zero_irk[16];
    const esp_ble_pid_keys_t *pid = &bond->bond_key.pid_key;
    *has_irk = (bond->bond_key.key_mask & ESP_BLE_ID_KEY_MASK)
        && memcmp(pid->irk, zero_irk, sizeof(zero_irk)) != 0;
    *type = *has_irk ? pid->addr_type : (bond->bd_addr_type & 1);
    memcpy(identity, *has_irk ? pid->static_addr : bond->bd_addr, 6);
    // A private address is not a stable whitelist entry. Never turn a
    // malformed identity into unrestricted pairing on reconnect.
    if (bond->bd_addr_type > BLE_ADDR_TYPE_RPA_RANDOM || *type > BLE_ADDR_TYPE_RANDOM
        || (*type == BLE_ADDR_TYPE_RANDOM && (identity[0] & 0xC0) != 0xC0))
        return ESP_ERR_INVALID_STATE;
    return ESP_OK;
}
static bool authenticated_saved_peer(const message_t *msg)
{
    esp_ble_bond_dev_t bond;
    uint8_t identity[6];
    esp_ble_addr_type_t type;
    bool has_irk;
    return find_bond(msg->peer, msg->addr_type, &bond) == ESP_OK
        && bond_identity(&bond, identity, &type, &has_irk) == ESP_OK
        && type == s.peer_identity_type && memcmp(identity, s.peer_identity, 6) == 0;
}
static bool bond_used_elsewhere(const esp_ble_bond_dev_t *bond)
{
    remote_t remote;
    for (unsigned i = 0; i < app_remote_count(); ++i)
        if (i != s.profile_index && app_remote_get(i, &remote) && remote.transport == REMOTE_BT
            && remote.bt.bonded && remote.bt.bond_store == s.profile.bt.bond_store
            && (remote.bt.bond_store == BT_BOND_STORE_LEGACY
                || memcmp(remote.bt.local_addr, s.profile.bt.local_addr, 6) == 0)
            && bond_matches(bond, remote.bt.peer, remote.bt.addr_type)) return true;
    return false;
}
static esp_err_t release_reports(void)
{
    bt_mouse_reset(&s.mouse, s.session ^ (uint32_t)esp_timer_get_time());
    portENTER_CRITICAL(&s_lock);
    s.mouse_generation = s_input_generation;
    s_snapshot.mouse_movement = s_snapshot.mouse_buttons = 0;
    portEXIT_CRITICAL(&s_lock);
    if (!s.hid || !s.connected || !s.encrypted || !s.subscribed || !physical_link(NULL)) return ESP_OK;
    uint8_t zero[8] = {0};
    if (s.profile.bt.kind == BT_REMOTE_MOUSE) {
        return esp_hidd_dev_input_set(s.hid, 0, BT_MOUSE_REPORT_ID, zero, BT_MOUSE_REPORT_SIZE);
    }
    (void)esp_hidd_dev_input_set(s.hid, 0, 1, zero, 8);
    (void)esp_hidd_dev_input_set(s.hid, 0, 2, zero, 2);
    return ESP_OK;
}
static esp_err_t disconnect_link(void)
{
    release_reports();
    uint8_t peer[6];
    esp_err_t err = physical_link(peer) ? esp_ble_gap_disconnect(peer) : ESP_OK;
    // CONNECT clears this bit; DISCONNECT sets it only after esp_hid has
    // finished its callback. Keep it set until another actual connection.
    bool down = (xEventGroupWaitBits(s_events, LINK_DOWN, pdFALSE, pdTRUE,
                                    pdMS_TO_TICKS(1500)) & LINK_DOWN) != 0;
    if (down) err = ESP_OK; // Also handles a peer disconnecting before our API call.
    else if (err == ESP_OK) err = ESP_ERR_TIMEOUT;
    s.connected = s.encrypted = s.subscribed = false;
    return err;
}
static esp_err_t stop_advertising(void)
{
    if (!s.advertising) return ESP_OK;
    if (physical_link(NULL)) { s.advertising = false; return ESP_OK; }
    xEventGroupClearBits(s_events, ADV_STOPPED);
    esp_err_t err = esp_ble_gap_stop_advertising();
    if (err == ESP_OK && !(xEventGroupWaitBits(s_events, ADV_STOPPED, pdTRUE, pdTRUE,
                                               pdMS_TO_TICKS(500)) & ADV_STOPPED)) err = ESP_ERR_TIMEOUT;
    s.advertising = false;
    return err;
}
static esp_err_t stop_runtime(void)
{
    if (s.controller_on || s.host_on) status(BT_STOPPING, ESP_OK);
    // Invalidate queued callbacks before teardown. Disconnect/stop completion
    // bits are still signalled directly, so they can safely be awaited here.
    portENTER_CRITICAL(&s_lock);
    s_callback_generation = 0;
    portEXIT_CRITICAL(&s_lock);
    s.start_deadline = s.pair_deadline = s.connect_deadline = s.save_deadline = 0;
    s.awaiting_save = s.forgetting = false;
    if (s.host_on) lifecycle_step("Stop adverts");
    esp_err_t result = stop_advertising();
    if (s.host_on) lifecycle_step("Disconnect");
    esp_err_t err = disconnect_link();
    // Do not destroy the HID event loop or disable Bluedroid underneath a
    // live/unfinished link. Return an actionable error instead of blocking
    // inside SDK shutdown, which would stall every subsequent remote open.
    if (err != ESP_OK) return err;
    if (result == ESP_OK) result = err;
    if (s.hid) {
        lifecycle_step("HID stop");
        (void)esp_hidd_dev_event_handler_unregister(s.hid, hid_callback, ESP_HIDD_ANY_EVENT);
        err = esp_hidd_dev_deinit(s.hid);
        if (err == ESP_OK) s.hid = NULL;
        if (result == ESP_OK) result = err;
    }
    if (s.host_on) {
        lifecycle_step("BT host off");
        err = esp_bluedroid_disable();
        if (err == ESP_OK) s.host_on = false;
        if (result == ESP_OK) result = err;
    }
    if (s.host_init && !s.host_on) {
        lifecycle_step("BT host deinit");
        err = esp_bluedroid_deinit();
        if (err == ESP_OK) s.host_init = false;
        if (result == ESP_OK) result = err;
    }
    if (s.controller_on && !s.host_on) {
        lifecycle_step("Controller off");
        err = esp_bt_controller_disable();
        if (err == ESP_OK) s.controller_on = false;
        if (result == ESP_OK) result = err;
    }
    if (s.controller_init && !s.controller_on && !s.host_init) {
        lifecycle_step("Controller end");
        err = esp_bt_controller_deinit();
        if (err == ESP_OK) s.controller_init = false;
        if (result == ESP_OK) result = err;
    }
    // A different profile is a different local identity. Recreate BOTH layers
    // so link-layer privacy/address state cannot outlive the selected key store.
    s.hid_ready = s.adv_ready = s.scan_ready = s.whitelist_ready = false;
    s.address_ready = s.privacy_ready = s.peer_has_irk = false;
    s.privacy_phase = PRIVACY_RESETTING;
    return result;
}
static void fail(esp_err_t error)
{
    const char *failed_step = s_operation;
    ESP_LOGE(TAG, "%s failed: %s (0x%x)", s_operation, esp_err_to_name(error), (unsigned)error);
    stop_runtime();
    s_operation = failed_step;
    status(BT_ERROR, error);
}
static esp_err_t start_stack(void)
{
    if (s.host_on || s.host_init || s.controller_on || s.hid) return ESP_ERR_INVALID_STATE;
    status(BT_STARTING, ESP_OK);
    s.start_deadline = esp_timer_get_time() + 10000000LL;
    ++s.generation;
    portENTER_CRITICAL(&s_lock);
    s_callback_generation = s.generation;
    portEXIT_CRITICAL(&s_lock);
    lifecycle_step("Controller init");
    esp_err_t err;
    if (!s.controller_init) {
        esp_bt_controller_config_t controller = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        if ((err = esp_bt_controller_init(&controller)) != ESP_OK) return err;
        s.controller_init = true;
    }
    lifecycle_step("Controller on");
    if ((err = esp_bt_controller_enable(ESP_BT_MODE_BLE)) != ESP_OK) return err;
    s.controller_on = true;

    // Public ESP-IDF API: one complete SMP database (including local IRK/ER)
    // per stable local identity. Never key this by the mutable profile index.
    if (s.profile.bt.bond_store == BT_BOND_STORE_LEGACY)
        snprintf(s.store_name, sizeof(s.store_name), "bt_config.conf");
    else if (remote_bt_has_identity(&s.profile)) {
        const uint8_t *a = s.profile.bt.local_addr;
        snprintf(s.store_name, sizeof(s.store_name), "ur%02x%02x%02x%02x%02x%02x",
                 a[0], a[1], a[2], a[3], a[4], a[5]);
    } else snprintf(s.store_name, sizeof(s.store_name), "ur_public");
    s_operation = "Bond storage";
    if ((err = esp_bt_config_file_path_update(s.store_name)) != ESP_OK) return err;
    lifecycle_step("BT host init");
    if ((err = esp_bluedroid_init()) != ESP_OK) return err;
    s.host_init = true;
    lifecycle_step("BT host on");
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
    const uint8_t *a = remote_bt_has_identity(&s.profile) ? s.profile.bt.local_addr : esp_bt_dev_get_address();
    if (a) ESP_LOGI(TAG, "Profile %u, %s, identity %02x:%02x:%02x:%02x:%02x:%02x",
        s.profile_index, s.store_name, a[0], a[1], a[2], a[3], a[4], a[5]);

    // Headless Just Works with bonding; negotiate Secure Connections when the
    // host supports it. No fake display/passkey requirement is imposed.
    s_operation = "Security";
    esp_ble_auth_req_t auth = ESP_LE_AUTH_REQ_SC_BOND;
    esp_ble_io_cap_t io = ESP_IO_CAP_NONE;
    uint8_t keys = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK, key_size = 16;
    if ((err = esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth, sizeof(auth))) != ESP_OK) return err;
    if ((err = esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &io, sizeof(io))) != ESP_OK) return err;
    if ((err = esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, 1)) != ESP_OK) return err;
    if ((err = esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &keys, 1)) != ESP_OK) return err;
    return esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &keys, 1);
}
static esp_err_t start_runtime(void)
{
    esp_err_t err = start_stack();
    if (err != ESP_OK) return err;
    s_operation = "Saved bond";
    esp_ble_bond_dev_t bond = {0};
    if (s.profile.bt.bonded) {
        err = find_bond(s.profile.bt.peer, s.profile.bt.addr_type, &bond);
        // Missing/corrupt saved keys require explicit Pair new, not an
        // unsolicited, unrestricted pairing window on a saved appliance.
        if (err != ESP_OK) return err;
    }
    s.pairing = !s.profile.bt.bonded;
    s.peer_has_irk = false;
    if (!s.pairing) {
        s_operation = "Peer identity";
        if ((err = bond_identity(&bond, s.peer_identity, &s.peer_identity_type, &s.peer_has_irk)) != ESP_OK)
            return err;
        ESP_LOGI(TAG, "Saved peer: identity type %u, IRK %s", (unsigned)s.peer_identity_type,
                 s.peer_has_irk ? "available" : "absent (fixed address)");
    }
    s.saved = !s.pairing;
    s.awaiting_save = s.forgetting = s.suspended = false;
    s.whitelist_ready = s.privacy_ready = false;
    s.privacy_phase = PRIVACY_RESETTING;
    // Reset retained controller policy for pairing as well as reconnecting.
    // The completion drives whitelist setup, then (if needed) peer privacy.
    s_operation = "Privacy reset";
    if ((err = esp_ble_gap_config_local_privacy(false)) != ESP_OK) return err;
    snprintf(s.name, sizeof(s.name), "UR %.12s", s.profile.name);
    s_operation = "Device name";
    if ((err = esp_ble_gap_set_device_name(s.name)) != ESP_OK) return err;
    s_operation = "Advertising";
    bool mouse = s.profile.bt.kind == BT_REMOTE_MOUSE;
    if ((err = bt_configure_advertising(mouse ? ESP_HID_APPEARANCE_MOUSE : ESP_HID_APPEARANCE_KEYBOARD)) != ESP_OK) return err;
    esp_hid_raw_report_map_t map = {.data = mouse ? bt_mouse_report_map : bt_keyboard_report_map,
        .len = mouse ? bt_mouse_report_map_size : bt_keyboard_report_map_size};
    esp_hid_device_config_t config = {
        .vendor_id = 0x303A, .product_id = mouse ? 0x4002 : 0x4001, .version = 0x0100,
        .device_name = s.name, .manufacturer_name = "Universal Remote",
        .serial_number = s.store_name, .report_maps = &map, .report_maps_len = 1,
    };
    lifecycle_step("HID service");
    return esp_hidd_dev_init(&config, ESP_HID_TRANSPORT_BLE, hid_callback, &s.hid);
}
static void advertise_if_ready(void)
{
    if (!s.hid_ready || !s.adv_ready || !s.scan_ready || !s.whitelist_ready || !s.address_ready || !s.privacy_ready
        || s.connected || s.advertising || s.awaiting_save || s.forgetting
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
static const uint8_t *local_identity(void)
{
    return remote_bt_has_identity(&s.profile) ? s.profile.bt.local_addr : esp_bt_dev_get_address();
}
static esp_err_t restore_subscriptions(void)
{
    s.ccc_persisted = false;
    if (s.pairing) return ESP_OK;
    esp_hidd_ccc_t ccc;
    esp_err_t err = bt_ccc_load(local_identity(), s.peer_identity, s.peer_identity_type, &ccc);
    if (err == ESP_ERR_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    err = esp_hidd_ble_restore_ccc(s.hid, &ccc);
    if (err == ESP_OK) {
        s.persisted_ccc = ccc;
        s.ccc_persisted = true;
        ESP_LOGI(TAG, "Restored bonded HID subscriptions (%u descriptors)", ccc.count);
    }
    return err;
}
static void ready_if_subscribed(void)
{
    if (!s.connected || !s.encrypted || !s.saved || s.awaiting_save) return;
    esp_hidd_ccc_t ccc;
    s_operation = "HID subscriptions";
    esp_err_t err = esp_hidd_ble_get_ccc(s.hid, &ccc);
    if (err == ESP_OK && (!s.ccc_persisted || memcmp(&ccc, &s.persisted_ccc, sizeof(ccc)))) {
        err = bt_ccc_save(local_identity(), s.peer_identity, s.peer_identity_type, &ccc);
        if (err == ESP_OK) {
            s.persisted_ccc = ccc;
            s.ccc_persisted = true;
        }
    }
    if (err != ESP_OK) { fail(err); return; }
    s.subscribed = false;
    for (unsigned i = 0; i < ccc.count; ++i)
        if (ccc.reports[i].map_index == 0 && ccc.reports[i].report_id == 1
            && ccc.reports[i].protocol_mode == ESP_HID_PROTOCOL_MODE_REPORT)
            s.subscribed = (ccc.reports[i].value & 1) != 0;
    if (s.subscribed) {
        s.connect_deadline = s.pair_deadline = 0;
        status(s.suspended ? BT_SUSPENDED : BT_CONNECTED, ESP_OK);
    } else {
        bt_mouse_reset(&s.mouse, s.session ^ (uint32_t)esp_timer_get_time());
        status(BT_CONNECTING, ESP_OK);
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
    if (s.profile.bt.kind == BT_REMOTE_KEYBOARD && output_active(&context) && app_remote_get(s.profile_index, &remote)) {
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
            esp_err_t restarted = stop_runtime();
            if (restarted == ESP_OK) restarted = start_runtime();
            if (restarted != ESP_OK) fail(restarted);
        }
    }
    app_post_evt(&result);
}
static void mouse_output(const message_t *msg, bool tick)
{
    if (s.profile.bt.kind != BT_REMOTE_MOUSE) return;
    output_context_t context = {.message = msg};
    remote_t remote;
    if (!output_active(&context) || (tick && s.suspended) || !app_remote_get(s.profile_index, &remote)) return;
    bt_output_t output = {.context = &context, .active = output_active, .send = output_send, .pause = output_pause};
    bool sent = tick ? bt_mouse_tick(&s.mouse, &remote, msg->time_ms, &output)
        : bt_mouse_input(&s.mouse, &remote, msg->key, msg->mouse_input, msg->time_ms, &output);
    portENTER_CRITICAL(&s_lock);
    s_snapshot.mouse_movement = s.mouse.movement;
    s_snapshot.mouse_buttons = s.mouse.buttons;
    portEXIT_CRITICAL(&s_lock);
    if (!sent && context.failed && desired_session() == s.session) {
        // A failed button release must never leave a drag running on the host.
        esp_err_t err = stop_runtime();
        if (err == ESP_OK) err = start_runtime();
        if (err != ESP_OK) fail(err);
    }
}
static bool release_input(void)
{
    if (release_reports() == ESP_OK) return true;
    // Even MODE cancellation must disconnect if the host missed button-up.
    esp_err_t err = stop_runtime();
    if (err == ESP_OK && desired_session() == s.session) err = start_runtime();
    if (err != ESP_OK) fail(err);
    return false;
}
static void worker(void *arg)
{
    for (;;) {
        message_t msg;
        if (xQueueReceive(s_queue, &msg, pdMS_TO_TICKS(BT_MOUSE_INTERVAL_MS)) == pdTRUE) {
            if (msg.type == MSG_OPEN) {
                if (msg.session != desired_session()) continue;
                esp_err_t err = stop_runtime();
                s.session = msg.session;
                s.profile = msg.remote;
                s.profile_index = msg.profile_index;
                if (err == ESP_OK) err = start_runtime();
                if (err != ESP_OK) fail(err);
            } else if (msg.type == MSG_CLOSE) {
                // The desired-session check below also handles a full queue.
            } else if (msg.type == MSG_FORGET && msg.session == s.session && msg.session == desired_session()) {
                esp_err_t err = stop_runtime();
                // Open only the selected bond store. No HID service, payload,
                // privacy/whitelist setup or advertising runs during removal.
                if (err == ESP_OK) err = start_stack();
                esp_ble_bond_dev_t bond;
                esp_err_t bond_err = s.profile.bt.bonded && err == ESP_OK
                    ? find_bond(s.profile.bt.peer, s.profile.bt.addr_type, &bond) : ESP_ERR_NOT_FOUND;
                if (err == ESP_OK && bond_err != ESP_OK && bond_err != ESP_ERR_NOT_FOUND) err = bond_err;
                if (err == ESP_OK && bond_err == ESP_OK && !bond_used_elsewhere(&bond)) {
                    s_operation = "Remove bond";
                    xEventGroupClearBits(s_events, BOND_REMOVED);
                    s_remove_result = ESP_FAIL;
                    err = esp_ble_remove_bond_device(bond.bd_addr);
                    if (err == ESP_OK) {
                        EventBits_t bits = xEventGroupWaitBits(s_events, BOND_REMOVED, pdTRUE, pdTRUE, pdMS_TO_TICKS(1500));
                        err = (bits & BOND_REMOVED) ? s_remove_result : ESP_ERR_TIMEOUT;
                    }
                }
                if (err != ESP_OK) fail(err);
                else {
                    // Subscription preferences belong to the removed bond.
                    s_operation = "Forget HID subscriptions";
                    err = bt_ccc_forget(local_identity());
                    if (err != ESP_OK) { fail(err); continue; }
                    s.start_deadline = 0;
                    s.forgetting = true;
                    s.pair_after = msg.flag;
                    post_peer(false, 0, NULL);
                }
            } else if (msg.type == MSG_SAVED && msg.session == s.session && s.awaiting_save) {
                s.awaiting_save = false;
                s.save_deadline = 0;
                if (!msg.flag) { fail(ESP_FAIL); continue; }
                if (s.forgetting) {
                    s.profile = msg.remote;
                    s.forgetting = false;
                    if (!s.pair_after) {
                        esp_err_t err = stop_runtime();
                        status(err == ESP_OK ? BT_UNPAIRED : BT_ERROR, err);
                    }
                    else {
                        // Only after durable profile storage select the isolated
                        // key store and advertise the SAME keyboard identity.
                        esp_err_t err = stop_runtime();
                        if (err == ESP_OK) err = start_runtime();
                        if (err != ESP_OK) fail(err);
                    }
                } else {
                    s.saved = true;
                    s.pairing = false;
                    // The host may disappear between authentication and the
                    // UI's durable-save acknowledgment. Resume its reconnect
                    // path rather than waiting forever without advertising.
                    if (!s.connected) {
                        esp_err_t err = stop_runtime();
                        if (err == ESP_OK) err = start_runtime();
                        if (err != ESP_OK) fail(err);
                    } else ready_if_subscribed();
                }
            } else if (msg.type == MSG_TAP && msg.session == s.session) tap(&msg);
            else if (msg.type == MSG_RELEASE && msg.session == s.session) release_input();
            else if (msg.type == MSG_MOUSE && msg.session == s.session) mouse_output(&msg, false);
            else if (msg.generation && msg.generation == s.generation && s.host_on) {
                if (desired_session() != s.session) continue;
                switch (msg.type) {
                    case MSG_HID_READY:
                        s_operation = "Restore HID subscriptions";
                        msg.error = restore_subscriptions();
                        s.hid_ready = msg.error == ESP_OK;
                        break;
                    case MSG_ADV_DATA: s_operation = "Adv payload"; s.adv_ready = msg.error == ESP_OK; break;
                    case MSG_SCAN_DATA: s_operation = "Scan response"; s.scan_ready = msg.error == ESP_OK; break;
                    case MSG_ADDRESS: s_operation = "BT identity"; s.address_ready = msg.error == ESP_OK; break;
                    case MSG_WHITELIST:
                        s_operation = "Saved peer";
                        if (msg.error == ESP_OK && msg.key == ESP_BLE_WHITELIST_CLEAR && !s.pairing) {
                            esp_ble_wl_addr_type_t type = s.peer_identity_type == BLE_ADDR_TYPE_RANDOM
                                ? BLE_WL_ADDR_TYPE_RANDOM : BLE_WL_ADDR_TYPE_PUBLIC;
                            esp_err_t err = esp_ble_gap_update_whitelist(true, s.peer_identity, type);
                            if (err != ESP_OK) fail(err);
                        } else if (msg.error == ESP_OK) {
                            s.whitelist_ready = true;
                            if (!s.peer_has_irk) {
                                s.privacy_phase = PRIVACY_READY;
                                s.privacy_ready = true;
                            } else {
                                // Bluedroid restores bonded IRKs into the S3 resolving list on enable.
                                // Device privacy accepts both the peer identity and its RPA, keeping
                                // fixed-address hosts working. Completion also checks the entry exists.
                                s_operation = "Peer privacy";
                                esp_err_t err = esp_ble_gap_set_privacy_mode(s.peer_identity_type,
                                    s.peer_identity, ESP_BLE_DEVICE_PRIVACY_MODE);
                                if (err != ESP_OK) fail(err);
                            }
                        }
                        break;
                    case MSG_PEER_PRIVACY:
                        s_operation = "Peer privacy";
                        if (msg.error == ESP_OK) {
                            s_operation = "Address resolution";
                            s.privacy_phase = PRIVACY_ENABLING;
                            esp_err_t err = esp_ble_gap_config_local_privacy(true);
                            if (err != ESP_OK) fail(err);
                        }
                        break;
                    case MSG_PRIVACY:
                        s_operation = s.privacy_phase == PRIVACY_RESETTING ? "Privacy reset" : "Address resolution";
                        if (msg.error == ESP_OK && s.privacy_phase == PRIVACY_RESETTING) {
                            s.privacy_phase = PRIVACY_PREPARING;
                            s_operation = "Clear peers";
                            esp_err_t err = esp_ble_gap_clear_whitelist();
                            if (err != ESP_OK) fail(err);
                        } else if (msg.error == ESP_OK && s.privacy_phase == PRIVACY_ENABLING) {
                            s.privacy_phase = PRIVACY_READY;
                            s.privacy_ready = true;
                            ESP_LOGI(TAG, "Peer address resolution enabled");
                        }
                        break;
                    case MSG_ADV_STARTED:
                        if (msg.error == ESP_OK) {
                            s.start_deadline = 0;
                            uint8_t address[6], type;
                            if (esp_ble_gap_get_local_used_addr(address, &type) == ESP_OK)
                                ESP_LOGI(TAG, "Advertising type %u at %02x:%02x:%02x:%02x:%02x:%02x",
                                    type, address[0], address[1], address[2], address[3], address[4], address[5]);
                            status(s.pairing ? BT_PAIRING : BT_RECONNECTING, ESP_OK);
                        }
                        break;
                    case MSG_CONNECTED:
                        if (!s.connected) {
                            memcpy(s.peer, msg.peer, 6);
                            s.connected = true; s.advertising = false;
                            s.connect_deadline = esp_timer_get_time() + 30000000LL;
                            ESP_LOGI(TAG, "GATT link established");
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
                        if (!s.pairing && !authenticated_saved_peer(&msg)) {
                            s_operation = "Unexpected peer";
                            fail(ESP_ERR_INVALID_STATE);
                            break;
                        }
                        if (s.pairing) {
                            // Keep the identity for later authentication events on this link too.
                            // Bluedroid commits bonding keys before delivering AUTH_CMPL.
                            esp_ble_bond_dev_t bond;
                            esp_err_t err = find_bond(msg.peer, msg.addr_type, &bond);
                            if (err == ESP_OK)
                                err = bond_identity(&bond, s.peer_identity, &s.peer_identity_type, &s.peer_has_irk);
                            if (err != ESP_OK) { fail(err); break; }
                        }
                        s.encrypted = true;
                        if (s.pairing) {
                            s.profile.bt.bonded = 1;
                            s.profile.bt.addr_type = s.peer_identity_type;
                            memcpy(s.profile.bt.peer, s.peer_identity, 6);
                            post_peer(true, s.peer_identity_type, s.peer_identity);
                        } else ready_if_subscribed();
                        break;
                    case MSG_DISCONNECTED:
                        bt_mouse_reset(&s.mouse, s.session ^ (uint32_t)esp_timer_get_time());
                        if (s.connected) {
                            s.connected = s.encrypted = s.subscribed = s.suspended = false;
                            s.connect_deadline = 0;
                            // Once paired, reconnect only to this profile's host.
                            if (s.saved && s.profile.bt.bonded) {
                                esp_err_t err = stop_runtime();
                                if (err == ESP_OK) err = start_runtime();
                                if (err != ESP_OK) fail(err);
                            } else status(BT_PAIRING, ESP_OK);
                        }
                        break;
                    case MSG_WRITE: ready_if_subscribed(); break;
                    case MSG_SUSPEND:
                        if (msg.flag && !release_input()) break;
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
            esp_err_t err = stop_runtime();
            status(err == ESP_OK ? BT_OFF : BT_ERROR, err);
            s.session = 0;
        }
        int64_t now = esp_timer_get_time();
        if (s.host_on && ((s.start_deadline && now >= s.start_deadline)
            || (s.save_deadline && now >= s.save_deadline))) {
            fail(ESP_ERR_TIMEOUT);
        }
        if (s.host_on && ((s.pair_deadline && now >= s.pair_deadline)
            || (s.connect_deadline && now >= s.connect_deadline))) {
            stop_runtime(); status(BT_PAIR_TIMEOUT, ESP_ERR_TIMEOUT);
        }
        // Generation cancellation also works when the release message could
        // not fit in the queue. Stale mouse presses cannot restart a pattern.
        portENTER_CRITICAL(&s_lock);
        uint32_t input_generation = s_input_generation;
        portEXIT_CRITICAL(&s_lock);
        if (s.profile.bt.kind == BT_REMOTE_MOUSE && s.mouse_generation != input_generation) release_input();
        message_t movement = {.session = s.session, .generation = input_generation, .time_ms = (uint32_t)(now / 1000)};
        mouse_output(&movement, true);
    }
}
void bt_keyboard_init(void)
{
    s_queue = xQueueCreate(32, sizeof(message_t));
    s_events = xEventGroupCreate();
    ESP_ERROR_CHECK(s_queue && s_events ? ESP_OK : ESP_ERR_NO_MEM);
    xEventGroupSetBits(s_events, LINK_DOWN);
    ESP_ERROR_CHECK(xTaskCreate(worker, "bt_keyboard", 6144, NULL, 5, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
bool bt_keyboard_open(const remote_t *remote, unsigned profile_index, uint32_t session)
{
    if (!remote || remote->transport != REMOTE_BT || remote->bt.kind > BT_REMOTE_MOUSE || !session) return false;
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
bool bt_keyboard_mouse_input(uint32_t session, unsigned key, bt_mouse_input_t input, uint32_t time_ms)
{
    bt_snapshot_t snapshot = bt_keyboard_snapshot();
    if (snapshot.session != session || desired_session() != session || !remote_key_assignable(key)
        || (snapshot.state != BT_CONNECTED && snapshot.state != BT_SUSPENDED)) return false;
    message_t msg = {.type = MSG_MOUSE, .session = session, .key = key, .mouse_input = input, .time_ms = time_ms};
    portENTER_CRITICAL(&s_lock);
    msg.generation = s_input_generation;
    portEXIT_CRITICAL(&s_lock);
    if (enqueue(&msg)) return true;
    // Losing a release is unsafe; cancel all mouse input even with a full queue.
    bt_keyboard_release(session);
    return false;
}
