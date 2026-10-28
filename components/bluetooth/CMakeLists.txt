idf_component_register(
  SRCS "bt_runtime.c" "bt_advertising.c" "bt_ccc_store.c"
  INCLUDE_DIRS "."
  REQUIRES app_core esp_hid
  PRIV_REQUIRES bt nvs_flash esp_timer
)
