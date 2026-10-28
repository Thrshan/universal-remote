// Compile the production worker itself; only ESP-IDF/RTOS boundaries are faked.
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../../main/bt_keyboard.c"

static unsigned checks;
#define CHECK(value) do { ++checks; if (!(value)) { fprintf(stderr, "Failed line %d: %s\n", __LINE__, #value); abort(); } } while (0)
struct host_queue { unsigned head, tail; message_t items[128]; };
static struct host_queue queue;
static jmp_buf idle;
static bool idle_tick, hold_mode, hold_resolution, hold_reset;
static int64_t now;
static esp_gap_ble_cb_t gap;
static esp_gatts_cb_t gatts;
static struct test_store {
    char name[16];
    esp_ble_bond_dev_t bonds[3];
    int count;
} stores[4], *active_store;
#define bonds (active_store->bonds)
#define bond_count (active_store->count)
static esp_err_t bond_read_error, mode_error, resolution_error;
static unsigned adv_calls, mode_calls, resolution_calls, privacy_reset_calls, hid_init_calls, init_calls, deinit_calls, removes, reports;
static esp_ble_adv_params_t advertised;
static uint8_t allowed[6], local_address[6], removed[6];
static esp_ble_wl_addr_type_t allowed_type;
static bool resolution_enabled;
static remote_t profiles[2];
static unsigned profile_count, saved_events;
static app_evt_t saved_event;
static esp_hidd_dev_t hid;
static esp_hidd_ccc_t hid_ccc, stored_ccc;
static bool has_ccc;
static unsigned controller_inits, controller_deinits, ccc_saves, ccc_restores, ccc_forgets;

static void gap_event(int event, int status_code)
{
    esp_ble_gap_cb_param_t param = {0};
    param.adv_data_cmpl.status = status_code;
    gap(event, &param);
}
static void whitelist_event(unsigned operation)
{
    esp_ble_gap_cb_param_t param = {.update_whitelist_cmpl = {.status = ESP_OK, .wl_operation = operation}};
    gap(ESP_GAP_BLE_UPDATE_WHITELIST_COMPLETE_EVT, &param);
}
static void run_worker(void)
{
    // Stop only when the real task has drained its queue and checked deadlines.
    idle_tick = true;
    if (setjmp(idle) == 0) worker(NULL);
}
QueueHandle_t xQueueCreate(unsigned count, unsigned size)
{ assert(size == sizeof(message_t)); memset(&queue, 0, sizeof(queue)); return &queue; }
int xQueueSend(QueueHandle_t q, const void *item, TickType_t timeout)
{
    assert(q->tail - q->head < 128);
    q->items[q->tail++ % 128] = *(const message_t *)item;
    return pdTRUE;
}
int xQueueReceive(QueueHandle_t q, void *item, TickType_t timeout)
{
    if (q->head != q->tail) { *(message_t *)item = q->items[q->head++ % 128]; return pdTRUE; }
    if (idle_tick) { idle_tick = false; return pdFALSE; }
    longjmp(idle, 1);
}
EventGroupHandle_t xEventGroupCreate(void) { return (void *)1; }
EventBits_t xEventGroupClearBits(EventGroupHandle_t group, EventBits_t bits) { return bits; }
EventBits_t xEventGroupSetBits(EventGroupHandle_t group, EventBits_t bits) { return bits; }
EventBits_t xEventGroupWaitBits(EventGroupHandle_t group, EventBits_t bits, int clear, int all, TickType_t timeout) { return bits; }
int xTaskCreate(void (*task)(void *), const char *name, unsigned stack, void *arg, unsigned priority, void *handle) { return pdPASS; }
void vTaskDelay(TickType_t delay) { now += delay * 1000LL; }
int64_t esp_timer_get_time(void) { return now; }
const char *esp_err_to_name(esp_err_t error) { return "test error"; }
esp_err_t esp_bt_controller_init(esp_bt_controller_config_t *config) { ++controller_inits; return ESP_OK; }
esp_err_t esp_bt_controller_enable(int mode) { return ESP_OK; }
esp_err_t esp_bt_controller_disable(void) { resolution_enabled = false; return ESP_OK; }
esp_err_t esp_bt_controller_deinit(void) { CHECK(!s.host_init && !s.controller_on); ++controller_deinits; return ESP_OK; }
esp_err_t esp_bt_config_file_path_update(const char *path)
{
    CHECK(!s.host_init && !s.host_on && strlen(path) < 16);
    for (unsigned i = 0; i < 4; ++i) {
        if (strcmp(stores[i].name, path) == 0 || !stores[i].name[0]) {
            snprintf(stores[i].name, sizeof(stores[i].name), "%s", path);
            active_store = &stores[i];
            return ESP_OK;
        }
    }
    return ESP_FAIL;
}
const uint8_t *esp_bt_dev_get_address(void) { static const uint8_t address[6] = {1,2,3,4,5,6}; return address; }
esp_err_t esp_bluedroid_init(void) { ++init_calls; return ESP_OK; }
esp_err_t esp_bluedroid_enable(void) { return ESP_OK; }
esp_err_t esp_bluedroid_disable(void) { return ESP_OK; }
esp_err_t esp_bluedroid_deinit(void) { ++deinit_calls; return ESP_OK; }
esp_err_t esp_ble_gap_register_callback(esp_gap_ble_cb_t callback) { gap = callback; return ESP_OK; }
esp_err_t esp_ble_gatts_register_callback(esp_gatts_cb_t callback) { gatts = callback; return ESP_OK; }
esp_err_t esp_ble_gap_set_rand_addr(uint8_t *address)
{ memcpy(local_address, address, 6); gap_event(ESP_GAP_BLE_SET_STATIC_RAND_ADDR_EVT, ESP_OK); return ESP_OK; }
esp_err_t esp_ble_gap_get_local_used_addr(uint8_t *address, uint8_t *type)
{ memcpy(address, local_address, 6); *type = BLE_ADDR_TYPE_RANDOM; return ESP_OK; }
esp_err_t esp_ble_gap_set_security_param(int parameter, void *value, uint8_t length) { return ESP_OK; }
esp_err_t esp_ble_gap_security_rsp(uint8_t *peer, bool accept) { return ESP_OK; }
int esp_ble_get_bond_device_num(void) { return bond_count; }
esp_err_t esp_ble_get_bond_device_list(int *count, esp_ble_bond_dev_t *list)
{ memcpy(list, bonds, *count * sizeof(*list)); return bond_read_error; }
esp_err_t esp_ble_remove_bond_device(uint8_t *peer)
{
    ++removes;
    memcpy(removed, peer, 6);
    for (int i = 0; i < bond_count; ++i) if (memcmp(peer, bonds[i].bd_addr, 6) == 0) {
        memmove(&bonds[i], &bonds[i+1], (bond_count-i-1)*sizeof(bonds[0]));
        --bond_count;
        break;
    }
    gap_event(ESP_GAP_BLE_REMOVE_BOND_DEV_COMPLETE_EVT, ESP_OK);
    return ESP_OK;
}
esp_err_t esp_ble_gap_clear_whitelist(void)
{ memset(allowed, 0, sizeof(allowed)); whitelist_event(ESP_BLE_WHITELIST_CLEAR); return ESP_OK; }
esp_err_t esp_ble_gap_update_whitelist(bool add, uint8_t *peer, esp_ble_wl_addr_type_t type)
{ assert(add); memcpy(allowed, peer, 6); allowed_type = type; whitelist_event(ESP_BLE_WHITELIST_ADD); return ESP_OK; }
esp_err_t esp_ble_gap_set_privacy_mode(esp_ble_addr_type_t type, uint8_t *peer, esp_ble_privacy_mode_t mode)
{
    ++mode_calls;
    CHECK(type == s.peer_identity_type && memcmp(peer, s.peer_identity, 6) == 0);
    CHECK(mode == ESP_BLE_DEVICE_PRIVACY_MODE);
    if (!mode_error && !hold_mode) gap_event(ESP_GAP_BLE_SET_PRIVACY_MODE_COMPLETE_EVT, ESP_OK);
    return mode_error;
}
esp_err_t esp_ble_gap_config_local_privacy(bool enable)
{
    if (!enable) {
        ++privacy_reset_calls;
        resolution_enabled = false;
        if (!hold_reset) gap_event(ESP_GAP_BLE_SET_LOCAL_PRIVACY_COMPLETE_EVT, ESP_OK);
        return ESP_OK;
    }
    ++resolution_calls;
    CHECK(s.whitelist_ready);
    resolution_enabled = !resolution_error;
    if (!resolution_error && !hold_resolution) gap_event(ESP_GAP_BLE_SET_LOCAL_PRIVACY_COMPLETE_EVT, ESP_OK);
    return resolution_error;
}
esp_err_t esp_ble_gap_set_device_name(const char *name) { return ESP_OK; }
esp_err_t bt_configure_advertising(uint16_t appearance)
{ gap_event(ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT, ESP_OK); gap_event(ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT, ESP_OK); return ESP_OK; }
esp_err_t esp_ble_gap_start_advertising(esp_ble_adv_params_t *params)
{
    CHECK(s.privacy_ready && (!s.peer_has_irk || resolution_enabled));
    ++adv_calls;
    advertised = *params;
    gap_event(ESP_GAP_BLE_ADV_START_COMPLETE_EVT, ESP_OK);
    return ESP_OK;
}
esp_err_t esp_ble_gap_stop_advertising(void) { gap_event(ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT, ESP_OK); return ESP_OK; }
esp_err_t esp_ble_gap_disconnect(uint8_t *peer)
{ esp_ble_gatts_cb_param_t param = {.disconnect.reason = 0x13}; gatts(ESP_GATTS_DISCONNECT_EVT, 0, &param); return ESP_OK; }
void esp_hidd_gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t interface, esp_ble_gatts_cb_param_t *param) {}
esp_err_t esp_hidd_dev_init(const esp_hid_device_config_t *config, int transport, esp_event_handler_t callback, esp_hidd_dev_t **device)
{ ++hid_init_calls; *device = &hid; callback(NULL, NULL, ESP_HIDD_START_EVENT, NULL); return ESP_OK; }
esp_err_t esp_hidd_dev_deinit(esp_hidd_dev_t *device) { return ESP_OK; }
esp_err_t esp_hidd_dev_event_handler_unregister(esp_hidd_dev_t *device, esp_event_handler_t callback, int32_t event) { return ESP_OK; }
esp_err_t esp_hidd_dev_input_set(esp_hidd_dev_t *device, size_t map, size_t report, uint8_t *data, size_t length)
{ ++reports; return ESP_OK; }
esp_err_t esp_hidd_ble_get_ccc(esp_hidd_dev_t *device, esp_hidd_ccc_t *ccc)
{ *ccc = hid_ccc; return ESP_OK; }
esp_err_t esp_hidd_ble_restore_ccc(esp_hidd_dev_t *device, const esp_hidd_ccc_t *ccc)
{ CHECK(!s.advertising && !s.connected); ++ccc_restores; hid_ccc = *ccc; return ESP_OK; }
esp_err_t bt_ccc_load(const uint8_t local[6], const uint8_t peer[6], uint8_t type, esp_hidd_ccc_t *ccc)
{ if (!has_ccc) return ESP_ERR_NOT_FOUND; *ccc = stored_ccc; return ESP_OK; }
esp_err_t bt_ccc_save(const uint8_t local[6], const uint8_t peer[6], uint8_t type, const esp_hidd_ccc_t *ccc)
{ CHECK(s.encrypted && s.saved && !s.awaiting_save); stored_ccc = *ccc; has_ccc = true; ++ccc_saves; return ESP_OK; }
esp_err_t bt_ccc_forget(const uint8_t local[6])
{ has_ccc = false; ++ccc_forgets; return ESP_OK; }
bool remote_bt_has_identity(const remote_t *remote) { return (remote->bt.local_addr[0] & 0xC0) == 0xC0; }
bool remote_key_assignable(unsigned key) { return key >= 1 && key <= REMOTE_KEY_COUNT && key != REMOTE_KEY_MODE; }
unsigned app_remote_count(void) { return profile_count; }
bool app_remote_get(unsigned index, remote_t *remote)
{ if (index >= profile_count) return false; *remote = profiles[index]; return true; }
bool app_remote_get_bt_text(unsigned index, unsigned key, char text[BT_TEXT_MAX + 1]) { return false; }
void app_post_evt(const app_evt_t *event) { ++saved_events; saved_event = *event; }

static void reset(bool bonded, bool private_peer)
{
    memset(&s, 0, sizeof(s));
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_desired_session = s_callback_generation = s_input_generation = 0;
    memset(stores, 0, sizeof(stores));
    active_store = &stores[0];
    strcpy(active_store->name, "urc10000000023");
    memset(profiles, 0, sizeof(profiles));
    bond_count = bonded ? 1 : 0;
    profile_count = 1;
    profiles[0].transport = REMOTE_BT;
    profiles[0].bt.bonded = bonded;
    profiles[0].bt.bond_store = BT_BOND_STORE_PROFILE;
    strcpy(profiles[0].name, "Keyboard");
    profiles[0].bt.local_addr[0] = 0xC1;
    profiles[0].bt.local_addr[5] = 0x23;
    bonds[0].bd_addr_type = private_peer ? BLE_ADDR_TYPE_RANDOM : BLE_ADDR_TYPE_PUBLIC;
    bonds[0].bd_addr[0] = private_peer ? 0x41 : 0x12;
    bonds[0].bd_addr[5] = 0x34;
    profiles[0].bt.addr_type = bonds[0].bd_addr_type;
    memcpy(profiles[0].bt.peer, bonds[0].bd_addr, 6);
    if (private_peer) {
        bonds[0].bond_key.key_mask = ESP_BLE_ID_KEY_MASK;
        bonds[0].bond_key.pid_key.addr_type = BLE_ADDR_TYPE_PUBLIC;
        bonds[0].bond_key.pid_key.static_addr[0] = 0xAB;
        bonds[0].bond_key.pid_key.static_addr[5] = 0xCD;
        memset(bonds[0].bond_key.pid_key.irk, 0x5A, 16);
    }
    now = 0;
    adv_calls = mode_calls = resolution_calls = init_calls = deinit_calls = removes = reports = saved_events = 0;
    privacy_reset_calls = hid_init_calls = 0;
    controller_inits = controller_deinits = ccc_saves = ccc_restores = ccc_forgets = 0;
    has_ccc = false;
    hid_ccc = (esp_hidd_ccc_t){.version = 1, .count = 1, .reports = {{0,1,ESP_HID_PROTOCOL_MODE_REPORT,1}}};
    hold_mode = hold_resolution = hold_reset = resolution_enabled = false;
    bond_read_error = mode_error = resolution_error = ESP_OK;
    memset(local_address, 0, 6);
    bt_keyboard_init();
}
static void open_profile(uint32_t session)
{ CHECK(bt_keyboard_open(&profiles[0], 0, session)); run_worker(); }
static void authenticate(const uint8_t *peer, esp_ble_addr_type_t type, bool success)
{
    esp_ble_gap_cb_param_t param = {0};
    // Model Bluedroid storing the keys before it reports successful bonding.
    if (success && s.pairing && !bond_count) {
        bond_count = 1;
        memcpy(bonds[0].bd_addr, peer, 6);
        bonds[0].bd_addr_type = type;
    }
    param.ble_security.auth_cmpl.success = success;
    param.ble_security.auth_cmpl.addr_type = type;
    memcpy(param.ble_security.auth_cmpl.bd_addr, peer, 6);
    gap(ESP_GAP_BLE_AUTH_CMPL_EVT, &param);
    run_worker();
}
static void connect_peer(const uint8_t *peer)
{
    esp_ble_gatts_cb_param_t param = {0};
    memcpy(param.connect.remote_bda, peer, 6);
    gatts(ESP_GATTS_CONNECT_EVT, 0, &param);
    run_worker();
    CHECK(s_snapshot.state == BT_CONNECTING && !s.encrypted && !reports);
}
static void expect_error(void)
{ CHECK(s_snapshot.state == BT_ERROR && !s.host_on && !s.advertising && !s.encrypted); }
int main(void)
{
    // Privacy setup is asynchronous: payload/service/whitelist readiness is insufficient.
    reset(true, true);
    hold_mode = hold_resolution = true;
    open_profile(1);
    CHECK(mode_calls == 1 && resolution_calls == 0 && adv_calls == 0);
    CHECK(allowed_type == BLE_WL_ADDR_TYPE_PUBLIC);
    CHECK(memcmp(allowed, bonds[0].bond_key.pid_key.static_addr, 6) == 0);
    gap_event(ESP_GAP_BLE_SET_PRIVACY_MODE_COMPLETE_EVT, ESP_OK);
    run_worker();
    CHECK(resolution_calls == 1 && adv_calls == 0 && !s.privacy_ready);
    gap_event(ESP_GAP_BLE_SET_LOCAL_PRIVACY_COMPLETE_EVT, ESP_OK);
    run_worker();
    CHECK(adv_calls == 1 && s_snapshot.state == BT_RECONNECTING);
    CHECK(advertised.adv_filter_policy == ADV_FILTER_ALLOW_SCAN_ANY_CON_WLST);
    CHECK(advertised.own_addr_type == BLE_ADDR_TYPE_RANDOM);
    CHECK(memcmp(local_address, profiles[0].bt.local_addr, 6) == 0);
    connect_peer(bonds[0].bond_key.pid_key.static_addr);
    authenticate(bonds[0].bond_key.pid_key.static_addr, BLE_ADDR_TYPE_PUBLIC, true);
    CHECK(s_snapshot.state == BT_CONNECTED && s.encrypted && reports == 0 && !saved_events);

    // Every reopen restores privacy, preserves the local identity and ignores old queued generations.
    for (unsigned i = 0; i < 3; ++i) {
        uint32_t old_generation = s.generation;
        bt_keyboard_close(); run_worker();
        CHECK(s_snapshot.state == BT_OFF && !s.privacy_ready && !s.host_on);
        hold_mode = true;
        open_profile(i + 2);
        unsigned before = adv_calls;
        message_t stale = {.type = MSG_PRIVACY, .generation = old_generation};
        CHECK(enqueue(&stale)); run_worker();
        CHECK(!s.privacy_ready && adv_calls == before);
        gap_event(ESP_GAP_BLE_SET_PRIVACY_MODE_COMPLETE_EVT, ESP_OK); run_worker();
        CHECK(adv_calls == before);
        gap_event(ESP_GAP_BLE_SET_LOCAL_PRIVACY_COMPLETE_EVT, ESP_OK); run_worker();
        CHECK(adv_calls == before + 1);
        CHECK(memcmp(local_address, profiles[0].bt.local_addr, 6) == 0);
    }
    CHECK(mode_calls == 4 && resolution_calls == 4 && init_calls == 4 && deinit_calls == 3);
    CHECK(controller_inits == 4 && controller_deinits == 3);

    // Fixed-address Windows-style bonds do not require an IRK.
    reset(true, false); open_profile(1);
    CHECK(!mode_calls && !resolution_calls && adv_calls == 1);
    CHECK(advertised.adv_filter_policy == ADV_FILTER_ALLOW_SCAN_ANY_CON_WLST);
    connect_peer(bonds[0].bd_addr);
    authenticate(bonds[0].bd_addr, BLE_ADDR_TYPE_PUBLIC, true);
    CHECK(s_snapshot.state == BT_CONNECTED);
    // Legacy profiles continue to use the public local address.
    reset(true, false); memset(profiles[0].bt.local_addr, 0, 6); strcpy(active_store->name, "ur_public"); open_profile(1);
    CHECK(advertised.own_addr_type == BLE_ADDR_TYPE_PUBLIC);

    // Hosts with an IRK can still reconnect using their fixed public identity.
    reset(true, true);
    bonds[0].bd_addr_type = BLE_ADDR_TYPE_PUBLIC;
    memcpy(bonds[0].bd_addr, bonds[0].bond_key.pid_key.static_addr, 6);
    profiles[0].bt.addr_type = BLE_ADDR_TYPE_PUBLIC;
    memcpy(profiles[0].bt.peer, bonds[0].bd_addr, 6);
    open_profile(1);
    CHECK(mode_calls == 1 && resolution_calls == 1);
    connect_peer(bonds[0].bd_addr);
    authenticate(bonds[0].bd_addr, BLE_ADDR_TYPE_PUBLIC, true);
    CHECK(s_snapshot.state == BT_CONNECTED);

    // An unexpected link loss rebuilds the same restricted privacy-enabled session.
    reset(true, true); open_profile(1);
    connect_peer(bonds[0].bd_addr);
    authenticate(bonds[0].bd_addr, BLE_ADDR_TYPE_RANDOM, true);
    CHECK(s_snapshot.state == BT_CONNECTED);
    esp_ble_gatts_cb_param_t disconnected = {.disconnect.reason = 0x08};
    gatts(ESP_GATTS_DISCONNECT_EVT, 0, &disconnected); run_worker();
    CHECK(s_snapshot.state == BT_RECONNECTING && mode_calls == 2 && resolution_calls == 2);
    CHECK(adv_calls == 2 && advertised.adv_filter_policy == ADV_FILTER_ALLOW_SCAN_ANY_CON_WLST);

    // Random identity lookup must work even if the database key is an older RPA.
    reset(true, true);
    bonds[0].bond_key.pid_key.addr_type = BLE_ADDR_TYPE_RANDOM;
    bonds[0].bond_key.pid_key.static_addr[0] = 0xC2;
    memcpy(profiles[0].bt.peer, bonds[0].bond_key.pid_key.static_addr, 6);
    open_profile(1);
    CHECK(allowed_type == BLE_WL_ADDR_TYPE_RANDOM && !s.pairing && adv_calls == 1);
    connect_peer(bonds[0].bd_addr);
    authenticate(bonds[0].bd_addr, BLE_ADDR_TYPE_RANDOM, true);
    CHECK(s_snapshot.state == BT_CONNECTED);
    // Explicit forgetting must remove the database entry, not an identity alias.
    CHECK(bt_keyboard_forget(1, false)); run_worker();
    CHECK(removes == 1 && memcmp(removed, profiles[0].bt.peer, 6) != 0);
    CHECK(adv_calls == 1 && hid_init_calls == 1 && s.forgetting);

    // Another bonded host cannot become the selected profile's authenticated host.
    reset(true, true); open_profile(1);
    bonds[1] = bonds[0]; bonds[1].bd_addr[5] ^= 1;
    bonds[1].bond_key.pid_key.static_addr[5] ^= 1; bond_count = 2;
    connect_peer(bonds[1].bd_addr);
    authenticate(bonds[1].bd_addr, BLE_ADDR_TYPE_RANDOM, true);
    expect_error(); CHECK(!reports && !saved_events);

    // Queue/API failure, controller rejection and lost completion all fail closed.
    reset(true, true); mode_error = ESP_FAIL; open_profile(1); expect_error(); CHECK(!adv_calls);
    reset(true, true); hold_mode = true; open_profile(1);
    gap_event(ESP_GAP_BLE_SET_PRIVACY_MODE_COMPLETE_EVT, 0x02); run_worker(); expect_error(); CHECK(!adv_calls);
    reset(true, true); resolution_error = ESP_FAIL; open_profile(1); expect_error(); CHECK(!adv_calls);
    reset(true, true); hold_resolution = true; open_profile(1);
    gap_event(ESP_GAP_BLE_SET_LOCAL_PRIVACY_COMPLETE_EVT, 0x0C); run_worker(); expect_error(); CHECK(!adv_calls);
    reset(true, true); hold_resolution = true; open_profile(1);
    now = 10000001; run_worker(); expect_error(); CHECK(s_snapshot.error == ESP_ERR_TIMEOUT && !adv_calls);
    reset(true, true); bond_read_error = ESP_FAIL; open_profile(1); expect_error(); CHECK(!adv_calls);
    reset(true, true); memset(bonds[0].bond_key.pid_key.irk, 0, 16); open_profile(1); expect_error(); CHECK(!adv_calls);
    reset(true, true); bonds[0].bond_key.pid_key.addr_type = BLE_ADDR_TYPE_RPA_RANDOM;
    open_profile(1); expect_error(); CHECK(!adv_calls);
    reset(true, true); bonds[0].bond_key.pid_key.addr_type = BLE_ADDR_TYPE_RANDOM;
    open_profile(1); expect_error(); CHECK(!adv_calls);

    // Fresh pairing and its durable-save handshake retain the existing flow.
    reset(false, false); open_profile(1);
    CHECK(s_snapshot.state == BT_PAIRING && !mode_calls && !resolution_calls);
    CHECK(advertised.adv_filter_policy == ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY);
    connect_peer(bonds[0].bd_addr);
    authenticate(bonds[0].bd_addr, BLE_ADDR_TYPE_PUBLIC, true);
    CHECK(s.awaiting_save && !s.saved && !reports && saved_events == 1);
    profiles[0].bt.bonded = 1;
    bt_keyboard_peer_saved(1, true, &profiles[0]); run_worker();
    CHECK(s_snapshot.state == BT_CONNECTED && reports == 0);
    authenticate(bonds[0].bd_addr, BLE_ADDR_TYPE_PUBLIC, true);
    CHECK(s_snapshot.state == BT_CONNECTED && saved_events == 1);
    // Pairing errors stay recoverable; explicit new pairing preserves another profile's bond.
    reset(false, false); open_profile(1); connect_peer(bonds[0].bd_addr);
    authenticate(bonds[0].bd_addr, BLE_ADDR_TYPE_PUBLIC, false);
    CHECK(s_snapshot.state == BT_PAIRING && !s.encrypted && !reports);
    reset(true, true);
    profiles[0].bt.bond_store = BT_BOND_STORE_LEGACY;
    strcpy(active_store->name, "bt_config.conf");
    profiles[1] = profiles[0]; profile_count = 2;
    // The other profile uses the canonical identity rather than the database alias.
    memcpy(profiles[1].bt.peer, bonds[0].bond_key.pid_key.static_addr, 6);
    profiles[1].bt.addr_type = BLE_ADDR_TYPE_PUBLIC;
    open_profile(1);
    CHECK(bt_keyboard_forget(1, true)); run_worker();
    CHECK(!removes && saved_events == 1 && !saved_event.bt.bonded);
    profiles[0].bt.bonded = 0;
    profiles[0].bt.bond_store = BT_BOND_STORE_PROFILE;
    bt_keyboard_peer_saved(1, true, &profiles[0]); run_worker();
    CHECK(s_snapshot.state == BT_PAIRING && !s.saved && !removes);
    CHECK(memcmp(local_address, profiles[0].bt.local_addr, 6) == 0);

    // Resetting privacy is mandatory even for a fresh profile after another host used the radio.
    reset(false, false); hold_reset = true; open_profile(1);
    CHECK(privacy_reset_calls == 1 && !adv_calls && !s.whitelist_ready);
    gap_event(ESP_GAP_BLE_SET_LOCAL_PRIVACY_COMPLETE_EVT, ESP_OK); run_worker();
    CHECK(adv_calls == 1 && advertised.adv_filter_policy == ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY);

    // Two local identities paired to the SAME host use separate SMP stores.
    // Switching/reordering/renaming profiles never changes which keys are loaded.
    reset(true, false);
    remote_t profile_a = profiles[0];
    uint8_t host_address[6]; memcpy(host_address, bonds[0].bd_addr, 6);
    open_profile(1); bt_keyboard_close(); run_worker();
    profile_count = 2; profiles[1] = profile_a;
    profiles[0].bt.local_addr[5] ^= 1;
    profiles[0].bt.bonded = 0;
    strcpy(profiles[0].name, "Second");
    open_profile(2);
    CHECK(active_store == &stores[1] && !bond_count && stores[0].count == 1);
    connect_peer(host_address);
    authenticate(host_address, BLE_ADDR_TYPE_PUBLIC, true);
    CHECK(s.awaiting_save && saved_event.bt.bonded);
    profiles[0].bt.bonded = 1;
    profiles[0].bt.addr_type = saved_event.bt.addr_type;
    memcpy(profiles[0].bt.peer, saved_event.bt.peer, 6);
    bt_keyboard_peer_saved(2, true, &profiles[0]); run_worker();
    CHECK(s_snapshot.state == BT_CONNECTED && stores[0].count == 1 && stores[1].count == 1);
    uint8_t stable_address[6]; memcpy(stable_address, local_address, 6);
    unsigned advertisements = adv_calls, services = hid_init_calls;
    CHECK(bt_keyboard_forget(2, true)); run_worker();
    CHECK(removes == 1 && stores[1].count == 0 && stores[0].count == 1);
    CHECK(adv_calls == advertisements && hid_init_calls == services && s.forgetting);
    profiles[0].bt.bonded = 0;
    bt_keyboard_peer_saved(2, true, &profiles[0]); run_worker();
    CHECK(adv_calls == advertisements + 1 && active_store == &stores[1]);
    CHECK(memcmp(local_address, stable_address, 6) == 0);
    bt_keyboard_close(); run_worker();
    strcpy(profiles[1].name, "Renamed");
    CHECK(bt_keyboard_open(&profiles[1], 1, 3)); run_worker();
    CHECK(active_store == &stores[0] && s_snapshot.state == BT_RECONNECTING);
    CHECK(memcmp(local_address, profile_a.bt.local_addr, 6) == 0 && stores[0].count == 1);

    // Windows can retain its CCC subscription and omit writes on reconnect.
    // Restore before advertising, then wait for authentication before readiness.
    reset(true, false); open_profile(1); connect_peer(bonds[0].bd_addr);
    hid_ccc.reports[0].value = 0;
    authenticate(bonds[0].bd_addr, BLE_ADDR_TYPE_PUBLIC, true);
    CHECK(!s.subscribed && s_snapshot.state == BT_CONNECTING && !reports);
    CHECK(ccc_saves == 1 && ccc_restores == 0);
    hid_ccc.reports[0].value = 1;
    callback_message((message_t){.type = MSG_WRITE}); run_worker();
    CHECK(s.subscribed && s_snapshot.state == BT_CONNECTED && ccc_saves == 2 && !reports);
    callback_message((message_t){.type = MSG_WRITE}); run_worker();
    CHECK(ccc_saves == 2); // LED/control writes must not cause flash wear.
    bt_keyboard_close(); run_worker();
    hid_ccc.reports[0].value = 0; // Newly allocated HID instance.
    open_profile(2);
    CHECK(ccc_restores == 1 && hid_ccc.reports[0].value == 1 && !s.encrypted && !s.subscribed);
    reports = 0;
    connect_peer(bonds[0].bd_addr);
    authenticate(bonds[0].bd_addr, BLE_ADDR_TYPE_PUBLIC, true);
    CHECK(s_snapshot.state == BT_CONNECTED && !reports && ccc_saves == 2);
    // A host explicitly disabling notifications takes effect and persists.
    hid_ccc.reports[0].value = 0;
    callback_message((message_t){.type = MSG_WRITE}); run_worker();
    CHECK(!s.subscribed && stored_ccc.reports[0].value == 0 && ccc_saves == 3);
    CHECK(bt_keyboard_forget(2, true)); run_worker();
    CHECK(ccc_forgets == 1 && !has_ccc);

    printf("PASS: %u Bluetooth worker checks (mock stack; hardware validation still required).\n", checks);
    return 0;
}


