# Universal Remote

ESP-IDF 5.5.2 firmware for an ESP32-S3 remote with a 21-key contact keypad,
SH1106 OLED (64x128 portrait), saved NEC IR remotes, and a BLE HID keyboard
remote with Fire TV, Media control, Text, and Custom keyboard presets, plus
mapped and pattern-based BLE mouse remotes.

- **IR:** create a named remote, choose an icon, learn each button, and replay NEC commands.
- **Bluetooth keyboard:** choose a preset after the icon, pair with a host, and
  reconnect to its saved host. Text assigns up to 64 characters per key; Custom
  offers a two-column standard-key picker with Ctrl/Shift/Alt combinations.
- **Bluetooth mouse:** choose Mouse mapped or Patterns. Use the D-pad to move,
  hold OK to drag in mapped mode, or toggle continuous patterns and left-button
  hold in Patterns. Movement speed is saved per remote.
- **Bluetooth controller:** visible in Add Remote, marked On hold.
- **Wi-Fi:** saved placeholder; transport not implemented.

Open a saved remote with **OK**, then press **MODE** to control it. MODE returns
to its menu. IR also supports pressing MODE directly on its home-screen tile.
Opening a Bluetooth tile enables Bluetooth in the background; switching between
its menu and Remote mode keeps the connection alive. BACK from the BT menu,
or HOME from its settings, exits the remote and turns Bluetooth off.

## Documentation

- **[User guide](docs/user-guide.md):** complete page-by-page instructions,
  every page's options and button actions, navigation flows, keyboard/mouse
  mappings, saved-data behavior and troubleshooting.
- **[Developer guide](docs/developer-guide.md):** architecture, task/queue
  ownership, all screen states, IR and Bluetooth lifecycles, storage contracts,
  diagnostics, testing and extension rules.
- Focused references: [UI overview](docs/ui.md),
  [Bluetooth setup and implementation](docs/bluetooth.md),
  [mouse controls](docs/bluetooth-mouse.md), and [keypad wiring](docs/keypad.md).

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
remote recordings and Bluetooth pairings are loaded from version-1 storage.
New saves use a version-3 snapshot that includes assigned text strings and
profile-specific Bluetooth storage. Mouse settings use spare profile bytes,
preserving the existing record size and all keyboard/IR remotes.

Run `tests/host/run.ps1` with Windows GCC for UI, storage, and key-report checks.
It also verifies 45 exact screen renders and saved-profile bytes against the
pre-reorganization firmware. A separate harness exercises the production Bluetooth
worker with SDK/RTOS substitutes. Actual pairing, reconnecting, and appliance key
delivery require the hardware checks in the Bluetooth guide.

## Source organization

`main/` contains startup, the application UI task, board drivers, and IR workers.
`components/app_core/` owns the shared remote model, events, application state,
storage, and Bluetooth profile mappings. `components/bluetooth/` owns the shared
keyboard/mouse Bluetooth runtime. `components/ui/` contains navigation, drawing,
and screens grouped by feature. Each component lists its sources and dependencies
in its own `CMakeLists.txt`; related source/header pairs stay together.

See [the architecture guide](docs/architecture.md) and the
[reorganization verification record](docs/reorganization-verification.md).
The [earlier project review](docs/history/project-review.md) is historical;
the unused experiments it describes have been removed.
