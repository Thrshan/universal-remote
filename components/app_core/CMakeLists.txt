idf_component_register(
  SRCS
    "app_state.c" "remote_model.c" "app_storage.c"
    "profiles/bt_keymap.c" "profiles/bt_mouse.c" "profiles/bt_output.c"
  INCLUDE_DIRS "." "profiles"
  REQUIRES freertos esp_common
  PRIV_REQUIRES nvs_flash esp_hw_support
)
