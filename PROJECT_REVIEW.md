# Universal Remote — Project Review

> ESP32-S3 IR universal remote with OLED UI and matrix keypad. Review covers
> current state, bugs, robustness, cleanup, and a roadmap for BLE HID + Wi-Fi
> IP control.

## Overall

The core architecture is genuinely good for an embedded project: concerns are
separated into a **UI task** (rendering + navigation), an **IR worker task**, a
**keypad scanner task**, and a shared **app_state** model guarded by a mutex,
with **queues** for commands/events. That producer/consumer + central model
pattern is exactly the right way to build this on FreeRTOS. The screen-stack
navigation in `components/ui/ui_nav.c` is clean and extensible.

The problems are mostly: **duplicate/conflicting definitions, dead files, a
couple of real GPIO/logic bugs, and an IR design that "works by accident."**
None are fatal, but they'll bite once BLE/Wi-Fi is added.

---

## Things that are broken or will break

### 1. Two conflicting definitions of the same types
`main/app_events.h` and `main/app_state.h` **both** define `ir_nec_code_t`,
`app_evt_t`, and `app_evt_type_t` — with *different* fields (`uint32_t` vs
`uint16_t`, different enum members). It survives only because `app_events.h` is
never included or compiled. The moment it is `#include`d anywhere, you get
redefinition errors. **Delete `main/app_events.h`** — it's superseded by
`main/app_state.h`.

### 2. `main/hid_ctrl.c` cannot compile
It references `g->flags`, `EVBIT_BT_ENABLED`, and `xEventGroupGetBits`, but
`app_ctx_t` in `main/app_state.h` has **no `flags` field**. It's saved only
because it's not in `main/CMakeLists.txt`. It's a stub. Keep it out of the build
until BLE is wired, and when you do, add an `EventGroupHandle_t flags;` to
`app_ctx_t`.

### 3. GPIO 10 is double-used
`main/oled.c` uses `GPIO 10` as `OLED_RESET_GPIO`, but `main/keypad.c` lists
`GPIO_NUM_10` as a keypad column input. Both drive/read the same pin. Either
move the OLED reset pin, set it to `-1` (most I2C OLEDs don't need it), or drop
GPIO 10 from `COLS`. This is a real hardware conflict that will cause phantom
keypresses or a stuck display.

### 4. `last_evt` is never written — the Learn screen's success path is dead
`components/ui/ui_screens.c` `ir_learn_render` reads `g->last_evt.type` to show
"Recorded!", but **nothing ever sets `s_app.last_evt`**. `app_post_evt` only
pushes to the queue (`main/app_state.c`). So the on-screen "Recorded!" relies on
a value that stays zero. The IR banner works (it's driven by
`ui_poll_app_events` in `main/app_ui_task.c`), but the Learn screen itself is
inconsistent. There are **two parallel event paths** (queue-drain in the UI task
vs. `last_evt` polling in the screen). Pick one — preferably route everything
through the queue-drain and set a flag/field on `ctx->st.ir_learn` there.

### 5. IR/NEC design "works by accident"
- The encoder in `main/ir_nec_encoder.c` sends `address` and `command` as
  **16-bit each** (`sizeof(uint16_t)`), and the decoder in `main/ir_nec.c` reads
  16 bits each. Because you learn → store raw 16-bit → resend the same 16 bits,
  the round-trip is self-consistent and will reproduce a captured remote. **But**
  it bypasses standard NEC's inverted-byte check (`addr + ~addr`, `cmd + ~cmd`),
  so there's no error detection, and the `0x%02X` labels in the UI imply 8-bit
  when you're really storing 16. It works for cloning, but it's not "NEC" in the
  validated sense.
- `nec_try_decode` rejects NEC **repeat frames** (the 9ms+2.25ms "held button"
  code), and the `signal_range_max_ns` / 64-symbol buffer only capture one
  frame. Fine for learning, but you can't distinguish protocols. If a remote
  isn't NEC (RC5, Sony SIRC, etc.), it silently fails to decode.

### 6. u8g2 full-buffer on a 4 KB stack
In `main/app_ui_task.c`, `u8g2_t u8;` is a **stack local**, and
`..._128x64_noname_f` (full-buffer mode) embeds a ~1 KB framebuffer inside that
struct. With a 4096-byte task stack plus `snprintf` and draw recursion, you're
uncomfortably close to overflow. Make it `static` (move it off the stack) or
bump the stack to 6–8 KB. Same goes for the 256-byte static buffer in the u8g2
I2C callback — fine, but watch total RAM as BLE/Wi-Fi land.

### 7. NVS blob has no version/CRC
`main/app_state.c` writes the raw `ir_nvs_blob_t` struct. Any change to
`MAX_IR_SLOTS`, `ir_slot_t`, or struct padding makes old data fail the
`size != sizeof(blob)` check and silently drop everything. Add a
`uint16_t version; uint16_t crc;` header and migrate on mismatch.

---

## Cleanup (low risk, high clarity gain)

- **Dead/duplicate source files**: `main/apppp_main.c`, `main/app_mainOG.c`,
  `main/main.c`, `main/hello_world_main.c`, `main/input_keypad.c` (empty), and
  the parallel menu system in `main/ui_menu.c` (not in the build; duplicates the
  menu in `components/ui/ui_screens.c`). Pick one of each and delete the rest —
  multiple `app_main` candidates are confusing and risky.
- **`components/New folder/`** and the three committed **`build/`,
  `build_keypad/`, `build_oled/`** directories should be removed from source
  control and added to `.gitignore`. Build artifacts in the repo bloat it and
  cause stale-config confusion.
- The commented-out alternate `oled_init` and pin maps add noise — once it
  works, delete the dead variants.

---

## Robustness recommendations

- Replace `ESP_ERROR_CHECK` in IR init/transmit paths (`main/ir_nec.c`) with
  graceful handling. `ESP_ERROR_CHECK` aborts/reboots the whole device on a
  recoverable RMT hiccup. You already do nice recovery
  (`rmt_disable`/`rmt_enable`) in the TX path — apply that pattern to init too.
- Add a **watchdog** (Task WDT) feeding for the long-lived tasks once networking
  is added, so a stuck Wi-Fi/BLE callback doesn't silently hang the UI.
- Centralize all GPIO assignments into **one header** (e.g. `board.h`) so
  conflicts like #3 are caught at a glance instead of hidden across files.
- The keypad emits on the queue with timeout 0 (`main/keypad.c` `emit_ui`) —
  fine, but if the UI task ever blocks (e.g. slow I2C), presses drop silently.
  Consider a small queue-depth log/counter during bring-up.

---

## Future: BLE HID + Wi-Fi IP control

This is the most important part for where the project is going, and the current
structure is *close* but IR-specific.

### Generalize the command model
Right now `app_cmd_t` (`main/app_state.h`) is IR-only. Make a stored "button"
transport-agnostic:

```c
typedef enum { XPORT_IR_NEC, XPORT_BLE_HID, XPORT_IP_HTTP } xport_t;

typedef struct {
    char     name[16];
    xport_t  transport;
    union {
        ir_nec_code_t ir;              // IR
        struct { uint16_t usage; } hid;// consumer/keyboard usage code
        struct { char url[96]; uint8_t method; } ip; // REST/WebSocket endpoint
    };
} action_t;
```

Then a **dispatcher** routes `action_t` to one of three backend tasks (the IR
worker you already have, a BLE-HID task, a Wi-Fi/IP task). The UI list
(`components/ui/ui_screens.c` `ir_list`) becomes a generic "actions" list — it
doesn't care how the action is delivered. This keeps the task/queue architecture
and makes each transport pluggable.

### BLE HID — important hardware fact
The **ESP32-S3 has BLE only, no Classic Bluetooth.** So "BT mouse/keyboard/
gamepad" must be **BLE HID**, not classic BT HID. That's fine — BLE HID
keyboard/mouse/gamepad all work and modern OSes pair to them. Use the **NimBLE**
stack (much smaller RAM/flash than Bluedroid, which matters once Wi-Fi is also
on). A single BLE HID device can expose a composite report map (keyboard +
mouse + consumer-control + gamepad) so you don't need to re-pair per mode.

### Wi-Fi + BLE coexistence
Wi-Fi and BLE share the one 2.4 GHz radio. ESP-IDF supports **software
coexistence** — enable it in menuconfig (`CONFIG_ESP_COEX_SW_COEXIST_ENABLE`).
It works well for HID-over-BLE while connected to Wi-Fi, but expect occasional
added latency. Budget for it.

### IP-based TV control
There's no single standard; implement per-brand backends behind the
`XPORT_IP_HTTP` transport:
- **Roku** — simplest: ECP, plain HTTP POST to
  `http://<ip>:8060/keypress/<Key>`.
- **Samsung (Tizen)** — WebSocket on `8001/8002` (TLS), token-based pairing.
- **LG (webOS)** — SSDP discovery + WebSocket (`ws://<ip>:3000`) with a
  client-key handshake.
- **Android TV / Google TV** — ADB or the Android TV remote protocol (mTLS,
  more involved).
- Many AVRs/TVs also accept generic HTTP/REST or Telnet.

Add **mDNS/SSDP discovery** so the device can find TVs on the LAN instead of
hard-coding IPs (IPs change with DHCP).

### Resource/partition planning (do this early)
Enabling Wi-Fi + NimBLE + u8g2 + the app will outgrow a default single-app
partition. Before starting:
- Use a **custom partition table** with a larger `factory`/app partition (and
  ideally OTA slots).
- Consider enabling **PSRAM** on the S3 module if the board has it — Wi-Fi+BLE
  buffers eat internal RAM fast.
- Keep an eye on free heap; NimBLE + Wi-Fi together can use 50–80 KB+.

### Security (matters once networking is on)
- Wi-Fi passwords, TV pairing tokens, and BLE bonds will live in NVS — enable
  **NVS encryption** (and eventually flash encryption + secure boot) so
  credentials aren't readable from a dumped flash.
- Validate/whitelist the IP targets; don't let a stored action POST to arbitrary
  URLs without bounds on the `url` field.

---

## Suggested order of work

1. Delete dead files + fix the GPIO 10 conflict + remove `app_events.h` (stops
   latent compile bombs).
2. Unify the IR event path (kill the `last_evt` dead path).
3. Add NVS versioning and move the u8g2 buffer off the stack.
4. Refactor `app_cmd_t` → generic `action_t` + dispatcher.
5. Add the BLE-HID (NimBLE) backend task.
6. Add Wi-Fi + one IP backend (start with Roku ECP — easiest to verify), enable
   coexistence.
7. Add discovery + NVS encryption.
