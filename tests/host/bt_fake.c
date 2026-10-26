#include "host_support.h"
#include "esp_gap_ble_api.h"
#include <string.h>

bt_snapshot_t host_bt_status;
unsigned host_bt_opens, host_bt_closes, host_bt_taps, host_bt_forgets, host_bt_releases;
bool host_bt_pair_after, host_bt_saved;
bt_key_report_t host_bt_report;
unsigned host_adv_calls, host_adv_fail_on;
uint16_t host_adv_service, host_adv_appearance;
bool host_adv_has_name, host_scan_has_name;

esp_err_t esp_ble_gap_config_adv_data(esp_ble_adv_data_t *data)
{
    ++host_adv_calls;
    // Match ESP-IDF 5.5.2 esp_gap_ble_api.c: this API rejects UUID lengths
    // that are not multiples of 16, even for a Bluetooth 16-bit service UUID.
    if (!data || (data->service_uuid_len & 0x0F)) return ESP_ERR_INVALID_ARG;
    if (host_adv_calls == host_adv_fail_on) return ESP_FAIL;
    if (data->set_scan_rsp) host_scan_has_name = data->include_name;
    else {
        host_adv_has_name = data->include_name;
        host_adv_appearance = data->appearance;
        static const uint8_t bluetooth_base[] = {0xFB,0x34,0x9B,0x5F,0x80,0x00,0x00,0x80,0x00,0x10,0x00,0x00};
        if (data->service_uuid_len == 16 && data->p_service_uuid
            && memcmp(data->p_service_uuid, bluetooth_base, sizeof(bluetooth_base)) == 0
            && data->p_service_uuid[14] == 0 && data->p_service_uuid[15] == 0)
            host_adv_service = data->p_service_uuid[12] | ((uint16_t)data->p_service_uuid[13] << 8);
    }
    return ESP_OK;
}
void bt_keyboard_init(void)
{
    host_bt_status = (bt_snapshot_t){0};
    host_bt_opens = host_bt_closes = host_bt_taps = host_bt_forgets = host_bt_releases = 0;
}
bool bt_keyboard_open(const remote_t *remote, unsigned profile_index, uint32_t session)
{
    if (remote->transport != REMOTE_BT || remote->bt.kind != BT_REMOTE_KEYBOARD) return false;
    ++host_bt_opens;
    host_bt_status = (bt_snapshot_t){.session = session,
        .state = remote->bt.bonded ? BT_RECONNECTING : BT_PAIRING};
    return true;
}
void bt_keyboard_close(void) { ++host_bt_closes; host_bt_status.state = BT_OFF; }
bool bt_keyboard_forget(uint32_t session, bool pair_after)
{
    if (session != host_bt_status.session) return false;
    ++host_bt_forgets;
    host_bt_pair_after = pair_after;
    return true;
}
void bt_keyboard_peer_saved(uint32_t session, bool success, const remote_t *remote)
{ if (session == host_bt_status.session) host_bt_saved = success; }
bool bt_keyboard_tap(uint32_t session, uint32_t request, unsigned key, bool long_hold)
{
    if (host_bt_status.session != session || (host_bt_status.state != BT_CONNECTED
        && host_bt_status.state != BT_SUSPENDED)) return false;
    if (!bt_keymap_report(key, long_hold, &host_bt_report)) return false;
    ++host_bt_taps;
    return true;
}
void bt_keyboard_release(uint32_t session) { ++host_bt_releases; }
bt_snapshot_t bt_keyboard_snapshot(void) { return host_bt_status; }
