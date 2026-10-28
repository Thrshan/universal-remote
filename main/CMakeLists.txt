idf_component_register(
  SRCS
    "app_main.c"
    "app/app_ui_task.c"
    "drivers/oled.c"
    "drivers/keypad.c"
    "ir/ir_nec.c"
    "ir/ir_nec_encoder.c"
  PRIV_INCLUDE_DIRS "app" "drivers" "ir"
  PRIV_REQUIRES app_core bluetooth ui u8g2 esp_driver_gpio esp_driver_i2c esp_timer esp_driver_rmt
)
