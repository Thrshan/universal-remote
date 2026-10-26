# Universal Remote

ESP-IDF 5.5.2 firmware for an ESP32-S3 remote with a 21-key contact keypad,
SH1106 OLED (64x128 portrait), saved NEC IR remotes, and a BLE HID keyboard
remote with a Fire TV preset.

- **IR:** create a named remote, choose an icon, learn each button, and replay NEC commands.
- **Bluetooth keyboard:** create a remote, pair with a host, reconnect to its saved
  host, inspect the fixed mappings, pair a new host, or unpair.
- **Bluetooth mouse/controller:** visible in Add Remote, marked On hold.
- **Wi-Fi:** saved placeholder; transport not implemented.

Open a saved remote with **OK**, then press **MODE** to control it. MODE returns
to its menu. IR also supports pressing MODE directly on its home-screen tile.
Opening a Bluetooth tile enables Bluetooth in the background; switching between
its menu and Remote mode keeps the connection alive. BACK from the BT menu,
or HOME from its settings, exits the remote and turns Bluetooth off.

See [the UI guide](docs/ui.md), [Bluetooth setup and mappings](docs/bluetooth.md),
and [keypad wiring](docs/keypad.md).

## Build and check

In an activated ESP-IDF 5.5.2 environment:

```text
idf.py set-target esp32s3
idf.py build
idf.py -p PORT flash monitor
```

The checked-in `sdkconfig.defaults` enables Bluedroid BLE, stored bonds, and the
larger single-app partition needed for the HID stack. Existing configurations
must enable these options too. No flash erase is needed for this change; existing
remote recordings retain their version-1 storage layout.

Run `tests/host/run.ps1` with Windows GCC for UI, storage, and key-report checks.
The host Bluetooth substitute verifies UI lifecycle calls; it does not emulate
the radio or prove compatibility with a physical Fire TV. Pairing, reconnecting,
and actual key delivery require the hardware checks in the Bluetooth guide.

Active firmware sources are listed in `main/CMakeLists.txt`. Older experiments
such as `hid_ctrl.c`, `app_events.h`, and alternate main files are not compiled.
