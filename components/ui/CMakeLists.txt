# ===================== components/ui/CMakeLists.txt =====================
idf_component_register(
  SRCS "ui_nav.c" "ui_screens.c" "ui_icons.c"
  INCLUDE_DIRS "include"
  PRIV_REQUIRES esp_driver_i2c u8g2 esp_app_format
  REQUIRES freertos esp_common main
)
