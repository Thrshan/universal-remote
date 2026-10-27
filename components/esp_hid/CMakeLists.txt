# Keep the SDK's HID implementation and database layout, replacing only the
# Bluedroid device source to add explicit, bond-scoped CCC save/restore support.
idf_build_get_property(idf_version IDF_VER)
if(NOT idf_version MATCHES "^v5\\.5\\.")
    message(FATAL_ERROR "The local esp_hid source is based on ESP-IDF 5.5.2; review it before changing SDK series")
endif()
file(TO_CMAKE_PATH "$ENV{IDF_PATH}/components/esp_hid" upstream)
set(srcs "${upstream}/src/esp_hidd.c" "${upstream}/src/esp_hidh.c" "${upstream}/src/esp_hid_common.c")
if(CONFIG_BT_BLUEDROID_ENABLED)
    list(APPEND srcs "src/ble_hidd.c" "${upstream}/src/ble_hidh.c" "${upstream}/src/bt_hidh.c" "${upstream}/src/bt_hidd.c")
endif()
if(CONFIG_BT_NIMBLE_ENABLED)
    list(APPEND srcs "${upstream}/src/nimble_hidd.c" "${upstream}/src/nimble_hidh.c")
endif()
idf_component_register(SRCS ${srcs} INCLUDE_DIRS "include" "${upstream}/include"
                       PRIV_INCLUDE_DIRS "${upstream}/private" REQUIRES esp_event bt)
target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-format")
