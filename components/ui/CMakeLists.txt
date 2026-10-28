idf_component_register(
  SRCS
    "ui_nav.c" "ui_screens.c" "ui_icons.c"
    "ui_common.c"
    "screens/ui_home.c"
    "screens/ui_settings.c"
    "screens/ui_remote.c"
    "screens/ui_edit.c"
    "screens/ui_wizard.c"
    "screens/ui_bluetooth.c"
    "screens/ui_ir.c"
  INCLUDE_DIRS "include"
  PRIV_INCLUDE_DIRS "."
  REQUIRES app_core bluetooth
  PRIV_REQUIRES u8g2 esp_timer esp_app_format
)
