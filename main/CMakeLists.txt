# idf_component_register(SRCS 
#                             "app_main.c" 
#                             "keypad.c"
#                             "app_state.c"
#                             "oled.c"
#                        PRIV_REQUIRES spi_flash esp_driver_gpio esp_driver_i2c
#                        INCLUDE_DIRS "")
# ===================== main/CMakeLists.txt =====================
idf_component_register(
  SRCS "app_main.c" "app_ui_task.c" "oled.c" "keypad.c" "app_state.c" "ir_nec.c" "ir_nec_encoder.c"
  INCLUDE_DIRS "."
  REQUIRES ui esp_driver_gpio esp_driver_i2c esp_timer esp_driver_rmt nvs_flash
)
