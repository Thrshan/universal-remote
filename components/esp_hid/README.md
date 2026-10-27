This project uses ESP-IDF 5.5.2's `esp_hid` component, with one local source replacement. All other implementation and headers still come from the selected SDK; the installed SDK is not modified.

`src/ble_hidd.c` originates from ESP-IDF v5.5.2 (Apache-2.0). Its changes are:

* Zero-initialize report descriptors, including their CCC state.
* Add `esp_hidd_ble_get_ccc` and `esp_hidd_ble_restore_ccc`, declared in `include/esp_hidd_ccc.h`. Restore validates the descriptor layout and notification bits, refuses connected devices, and updates both the GATT attributes and HID's internal notification state.

The application saves actual host-written subscriptions after successful authentication and durable profile storage. It restores them only for that profile's selected bond, before advertising, and deletes them on explicit unpair. This follows bonded CCC persistence rather than unconditionally enabling notifications. The report map and GATT service layout remain those of the SDK.

Background: https://github.com/espressif/esp-idf/issues/13054 describes the missing CCC persistence and Espressif's recommended solution. A project-local component keeps the fix reviewable and reproducible. CMake stops on a different SDK series so an upgrade requires reviewing the replacement source.
