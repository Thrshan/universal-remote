#pragma once
// Host-only surface for compiling the real Bluetooth worker with deterministic stack events.
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "../../include/esp_gap_ble_api.h"
#define ESP_ERR_NOT_FOUND 7
#define ESP_ERR_TIMEOUT 8
#define BIT0 1
#define BIT1 2
#define BIT2 4
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
typedef unsigned EventBits_t;
typedef void *EventGroupHandle_t;
EventGroupHandle_t xEventGroupCreate(void);
EventBits_t xEventGroupClearBits(EventGroupHandle_t group, EventBits_t bits);
EventBits_t xEventGroupSetBits(EventGroupHandle_t group, EventBits_t bits);
EventBits_t xEventGroupWaitBits(EventGroupHandle_t group, EventBits_t bits, int clear, int all, TickType_t timeout);
int xTaskCreate(void (*task)(void *), const char *name, unsigned stack, void *arg, unsigned priority, void *handle);
void vTaskDelay(TickType_t delay);
typedef struct { int unused; } esp_bt_controller_config_t;
#define BT_CONTROLLER_INIT_CONFIG_DEFAULT() ((esp_bt_controller_config_t){0})
#define ESP_BT_MODE_BLE 1
esp_err_t esp_bt_controller_init(esp_bt_controller_config_t *config);
esp_err_t esp_bt_controller_enable(int mode);
esp_err_t esp_bt_controller_disable(void);
esp_err_t esp_bt_controller_deinit(void);
esp_err_t esp_bluedroid_init(void);
esp_err_t esp_bluedroid_enable(void);
esp_err_t esp_bluedroid_disable(void);
esp_err_t esp_bluedroid_deinit(void);
typedef enum { BLE_ADDR_TYPE_PUBLIC, BLE_ADDR_TYPE_RANDOM, BLE_ADDR_TYPE_RPA_PUBLIC, BLE_ADDR_TYPE_RPA_RANDOM } esp_ble_addr_type_t;
typedef enum { BLE_WL_ADDR_TYPE_PUBLIC, BLE_WL_ADDR_TYPE_RANDOM } esp_ble_wl_addr_type_t;
typedef enum { ESP_BLE_NETWORK_PRIVACY_MODE, ESP_BLE_DEVICE_PRIVACY_MODE } esp_ble_privacy_mode_t;
#define ESP_BLE_ENC_KEY_MASK 1
#define ESP_BLE_ID_KEY_MASK 2
typedef struct { uint8_t irk[16]; esp_ble_addr_type_t addr_type; uint8_t static_addr[6]; } esp_ble_pid_keys_t;
typedef struct { uint8_t key_mask; esp_ble_pid_keys_t pid_key; } esp_ble_bond_key_info_t;
typedef struct { uint8_t bd_addr[6]; esp_ble_bond_key_info_t bond_key; esp_ble_addr_type_t bd_addr_type; } esp_ble_bond_dev_t;
typedef int esp_ble_auth_req_t;
typedef int esp_ble_io_cap_t;
#define ESP_LE_AUTH_BOND 1
#define ESP_LE_AUTH_REQ_SC_BOND 9
esp_err_t esp_bt_config_file_path_update(const char *path);
const uint8_t *esp_bt_dev_get_address(void);
#define ESP_IO_CAP_NONE 3
enum { ESP_BLE_SM_AUTHEN_REQ_MODE, ESP_BLE_SM_IOCAP_MODE, ESP_BLE_SM_MAX_KEY_SIZE, ESP_BLE_SM_SET_INIT_KEY, ESP_BLE_SM_SET_RSP_KEY };
enum { ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT, ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT,
    ESP_GAP_BLE_UPDATE_WHITELIST_COMPLETE_EVT, ESP_GAP_BLE_ADV_START_COMPLETE_EVT,
    ESP_GAP_BLE_SET_STATIC_RAND_ADDR_EVT, ESP_GAP_BLE_SET_PRIVACY_MODE_COMPLETE_EVT,
    ESP_GAP_BLE_SET_LOCAL_PRIVACY_COMPLETE_EVT, ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT,
    ESP_GAP_BLE_REMOVE_BOND_DEV_COMPLETE_EVT, ESP_GAP_BLE_SEC_REQ_EVT, ESP_GAP_BLE_AUTH_CMPL_EVT };
typedef int esp_gap_ble_cb_event_t;
#define ESP_BT_STATUS_SUCCESS 0
#define ESP_BLE_WHITELIST_REMOVE 0
#define ESP_BLE_WHITELIST_ADD 1
#define ESP_BLE_WHITELIST_CLEAR 2
typedef struct { int status; } bt_status_param_t;
typedef union {
    bt_status_param_t adv_data_cmpl, scan_rsp_data_cmpl, adv_start_cmpl, set_rand_addr_cmpl,
        set_privacy_mode_cmpl, local_privacy_cmpl, remove_bond_dev_cmpl;
    struct { int status; unsigned wl_operation; } update_whitelist_cmpl;
    union {
        struct { uint8_t bd_addr[6]; } ble_req;
        struct { bool success; uint8_t fail_reason; uint8_t bd_addr[6]; esp_ble_addr_type_t addr_type; } auth_cmpl;
    } ble_security;
} esp_ble_gap_cb_param_t;
typedef void (*esp_gap_ble_cb_t)(esp_gap_ble_cb_event_t, esp_ble_gap_cb_param_t *);
esp_err_t esp_ble_gap_register_callback(esp_gap_ble_cb_t callback);
esp_err_t esp_ble_gap_set_rand_addr(uint8_t *address);
esp_err_t esp_ble_gap_get_local_used_addr(uint8_t *address, uint8_t *type);
esp_err_t esp_ble_gap_set_security_param(int parameter, void *value, uint8_t length);
esp_err_t esp_ble_gap_security_rsp(uint8_t *peer, bool accept);
int esp_ble_get_bond_device_num(void);
esp_err_t esp_ble_get_bond_device_list(int *count, esp_ble_bond_dev_t *bonds);
esp_err_t esp_ble_remove_bond_device(uint8_t *peer);
esp_err_t esp_ble_gap_clear_whitelist(void);
esp_err_t esp_ble_gap_update_whitelist(bool add, uint8_t *peer, esp_ble_wl_addr_type_t type);
esp_err_t esp_ble_gap_set_privacy_mode(esp_ble_addr_type_t type, uint8_t *peer, esp_ble_privacy_mode_t mode);
esp_err_t esp_ble_gap_config_local_privacy(bool enable);
esp_err_t esp_ble_gap_set_device_name(const char *name);
#define ADV_TYPE_IND 0
#define ADV_CHNL_ALL 7
#define ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY 0
#define ADV_FILTER_ALLOW_SCAN_ANY_CON_WLST 2
typedef struct { unsigned adv_int_min, adv_int_max, adv_type; esp_ble_addr_type_t own_addr_type; unsigned channel_map, adv_filter_policy; } esp_ble_adv_params_t;
esp_err_t esp_ble_gap_start_advertising(esp_ble_adv_params_t *params);
esp_err_t esp_ble_gap_stop_advertising(void);
esp_err_t esp_ble_gap_disconnect(uint8_t *peer);
typedef int esp_gatts_cb_event_t;
typedef int esp_gatt_if_t;
enum { ESP_GATTS_CONNECT_EVT, ESP_GATTS_DISCONNECT_EVT, ESP_GATTS_WRITE_EVT };
typedef union { struct { uint8_t remote_bda[6]; } connect; struct { unsigned reason; } disconnect; } esp_ble_gatts_cb_param_t;
typedef void (*esp_gatts_cb_t)(esp_gatts_cb_event_t, esp_gatt_if_t, esp_ble_gatts_cb_param_t *);
esp_err_t esp_ble_gatts_register_callback(esp_gatts_cb_t callback);
void esp_hidd_gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t interface, esp_ble_gatts_cb_param_t *param);
typedef const char *esp_event_base_t;
typedef struct { int unused; } esp_hidd_dev_t;
typedef void (*esp_event_handler_t)(void *, esp_event_base_t, int32_t, void *);
enum { ESP_HIDD_START_EVENT, ESP_HIDD_CONTROL_EVENT, ESP_HIDD_ANY_EVENT = -1 };
#define ESP_HID_TRANSPORT_BLE 1
#define ESP_HID_APPEARANCE_KEYBOARD 0x03C1
#define ESP_HID_PROTOCOL_MODE_REPORT 1
typedef struct { struct { unsigned control; } control; } esp_hidd_event_data_t;
typedef struct { const uint8_t *data; size_t len; } esp_hid_raw_report_map_t;
typedef struct { unsigned vendor_id, product_id, version; const char *device_name, *manufacturer_name, *serial_number; esp_hid_raw_report_map_t *report_maps; unsigned report_maps_len; } esp_hid_device_config_t;
esp_err_t esp_hidd_dev_init(const esp_hid_device_config_t *config, int transport, esp_event_handler_t callback, esp_hidd_dev_t **device);
esp_err_t esp_hidd_dev_deinit(esp_hidd_dev_t *device);
esp_err_t esp_hidd_dev_event_handler_unregister(esp_hidd_dev_t *device, esp_event_handler_t callback, int32_t event);
esp_err_t esp_hidd_dev_input_set(esp_hidd_dev_t *device, size_t map, size_t report, uint8_t *data, size_t length);
