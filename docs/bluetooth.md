# Bluetooth keyboard remote

![Actual OLED screen renders](bluetooth-preview.png)

![Keyboard presets and assignment screens](bluetooth-presets-preview.png)

## Create and use

1. Choose **Add remote > BT > Keyboard**. Mouse and Controller display **On hold**
   and cannot advance or create a remote.
2. Enter a name and select an icon, then choose **Fire TV**, **Media control**,
   **Text**, or **Custom**. Accepting the preset saves the remote and starts
   Bluetooth. Text and Custom open key assignment; MODE finishes assignment and
   opens the connection menu. BACK from the preset chooser returns to the icon;
   HOME discards the unfinished draft. No radio is enabled for an unfinished draft.
3. The OLED shows **Starting BT**, then **BT searching** for a new profile. Open
   Bluetooth accessory pairing on the Fire TV and select **UR <remote name>**.
4. **Connecting** means a link exists but encryption, profile storage, or keyboard
   notification setup is still pending. **Connected** means keyboard reports can
   be sent. Verify the host's responses on the actual Fire TV.
5. Press **MODE** to enter Remote mode. All mapped keys now control the host;
   BACK and HOME are appliance commands. MODE returns to the same BT menu with
   Bluetooth still enabled. Settings navigation never sends appliance keys.
6. BACK from the BT menu returns to home and shuts down the HID service,
   disables the Bluetooth host and controller. HOME from BT settings also exits and shuts
   Bluetooth down. Pairing information remains saved.

From home, open the saved BT tile with OK before pressing MODE. MODE on that
home tile shows **OK:open first** and does not enable the radio. IR keeps its
direct MODE shortcut and also supports OK, then MODE, from its editor.

## Connection menu

Use UP/DOWN and OK. Five entries scroll through the three visible rows:

- **Reconnect:** reopen the saved host connection, or restart discovery after an
  error/timeout. An already connected session stays connected.
- **Pair new:** confirm forgetting this profile's previous host, then open a new
  two-minute pairing window. Select the remote from the new host's pairing UI.
- **Unpair:** confirm forgetting this profile's peer and stop Bluetooth. Reconnect
  or Pair new can then begin fresh pairing. If the host retains a stale pairing,
  forget the accessory on the host as well before retrying.
- **Key map:** inspect the Fire TV or Media control preset, or edit Text/Custom
  assignments by pressing the physical key to map. MODE finishes assignment;
  holding BACK returns to the menu. HOME/BACK can themselves be assigned.
- **Edit remote:** change its name/icon or remove it. Removing a BT remote also
  clears its pairing before deleting the profile. Cancel is selected by default.
  A changed advertised name takes effect the next time the remote is opened.

Opening a previously paired profile advertises only for that profile's saved
peer using the controller whitelist. Disconnection restarts reconnection while
the remote stays open. Fresh pairing has a two-minute timeout; initialization,
authentication, storage, and connection-setup failures are reported instead of
enabling key sending on an unauthenticated link. Reconnect retries the session.
The peripheral advertises for a host to connect; it does not scan a list of TVs.
On BT error, the menu shows the failed startup step and the numeric error code;
the serial log records the same step and the ESP-IDF error name.

Each profile stores its host identity separately. ESP-IDF stores encryption keys
in NVS. Forgetting a profile preserves a shared host bond if another saved profile
still uses it. Unrelated remotes and their recordings remain intact.
New keyboard profiles also store their own static-random BLE address. That
address stays fixed across exit, reconnect, and reboot. Explicit Unpair/Pair new
replaces it only after the new identity is durably saved, so a host can discover
a fresh accessory instead of reusing a stale HID/security cache. Legacy paired
profiles retain their public-address identity until explicitly unpaired.
Failed fresh-pairing attempts keep the pairing window open and show Retry on
host; no key reports are allowed until a later authentication succeeds.

## Keyboard presets

**Fire TV** preserves the mappings below, including its special Power hold.
**Media control** maps PLAY/PAUSE to media play/pause, VOL UP/DOWN to volume,
VOL CLICK to mute, CH UP/DOWN to next/previous track, CH CLICK to stop, CUST BTN
2/3 to rewind/fast-forward, and POWER to Consumer Power. It also includes the
arrow keys, Enter on OK, Esc on BACK, and launcher Home on HOME. The remaining
three custom buttons are unassigned. Each command includes a release report.

**Custom** starts with empty bindings. Press a physical key, then choose from
the two-column list of letters, digits, punctuation, navigation keys, F1–F24,
keypad keys and standard left/right modifiers. Arrows move through the list;
OK saves and BACK cancels. While choosing, **CUST BTN 4 toggles Ctrl**,
**CUST BTN 5 toggles Shift**, and **CUST BTN 6 toggles Alt**. They can be combined
and toggled off; the active choices are shown above the list. Select Clear to
unassign a key. Modifier choices are saved per binding, loaded when editing that
binding again, and reset for an unassigned key. The modifier shortcuts apply
only inside this editor; CUST BTN 4/5/6 remain assignable physical keys.

**Text** starts empty. Press a physical key to enter its string with the screen
keyboard. Custom buttons and ^ cycle abc/ABC/123/symbols; the underscore cell
inserts a space, < deletes and -> saves. BACK cancels. Saving empty text clears
the assignment. Up to **64 characters per key** are supported; spaces are kept.
Text uses standard US-layout keyboard reports for ASCII letters, digits and
punctuation, so the connected host should use a US keyboard layout. A press in
Remote mode types the string character by character, including a release after
each character (40 ms press, 10 ms gap). Repeated arrow/channel events do not
retype whole strings. MODE or leaving the remote cancels remaining characters;
an attempted press is released even when sending fails or playback is cancelled.

Text/Custom changes are saved before confirming success. Save failures retain
the existing binding/text and keep the editor open for retry. Updated mappings
are used on the current Bluetooth connection without re-pairing. Only Fire TV
uses the special short/held Power behavior; other presets use POWER like an
ordinary assignable key. MODE always controls the local UI.

## Fire TV mapping

| Physical remote button | HID signal |
| --- | --- |
| BACK | Esc (the selected alternative to F2) |
| HOME | Consumer AC Home, the house/launcher key |
| CUST BTN 4 | Keyboard Application/Menu, the reference keyboard's Fn+Ctrl function |
| CH UP | F7 |
| PLAY_PAUSE | F8 |
| CH DOWN | F9 |
| UP / DOWN / LEFT / RIGHT | Corresponding arrow key |
| OK | Enter |
| POWER, short press | Keyboard PC Power, sent on release |
| POWER, hold for 700 ms | Ctrl+Alt+Delete, sent once; no short Power signal |

Fn is a reference keyboard's local layer selection, not an extra modifier to
transmit. Home uses Consumer usage `0x0223`, Menu uses Keyboard `0x65`, and PC
Power uses Keyboard `0x66`, following the [Android HID mapping table](https://source.android.com/docs/core/interaction/input/keyboard-devices).
Keyboard Home (`0x4A`) would move a text cursor rather than open the launcher.
F7/F8/F9 remain exactly the requested function keys.

The remaining eight appliance buttons are unassigned. MODE always belongs to
the local UI. Every transmitted press is followed by a matching zero/release
report. Arrow/channel repeats come from the existing keypad repeat behavior;
modifier and Power holds are not left asserted. Switching MODE cancels a
pending short Power press and releases reports.
An explicit mapped button press is also allowed while the host has suspended
HID, using ESP-IDF's advertised remote-wake capability. Host wake behavior must
be checked on the Fire TV.

## Implementation and verification

`main/bt_keyboard.c` owns Bluetooth on one worker task. The UI requests sessions,
reads status snapshots, and commits peer identities to the existing profile
store. Callback generations/session IDs reject stale events. Connected status
requires successful encryption, durable peer storage, and a subscribed keyboard
input report. The HID descriptor has an eight-byte keyboard report (ID 1) and
a two-byte Consumer Control report (ID 2). There is no automatic volume demo.
The controller is initialized once and enabled only during an open BT
session. Bluedroid is disabled and deinitialized on exit, then initialized again
on the next visit; keeping only a disabled host led to a privacy-list crash in
ESP-IDF 5.5.2. Exit disables the controller/radio without using the final-deinit
API as a routine mode switch. Each session clears the controller whitelist
before allowing the selected saved peer, or all peers during fresh pairing.

BT metadata and Custom bindings overlay the unused IR-code area of BT profiles.
`remote_t` remains 104 bytes with the same offsets. Existing version-1 profiles
load as Fire TV; new saves use a version-2 snapshot containing those same profile
records followed by only the assigned text strings. A checksum covers the entire
snapshot, including strings. Text, mappings and peer metadata share a single NVS
save, with RAM rollback on failure. IR and legacy recording migration are retained.

The COM7 hardware check on 2026-10-26 confirmed advertising, encrypted pairing,
automatic reconnection after leaving/reopening the saved remote, and controller
shutdown on exit. The startup `ESP_ERR_INVALID_ARG` was caused by passing a
two-byte service UUID to Bluedroid's structured advertising API; that API needs
the 128-bit Bluetooth-base representation even for the 16-bit HID service.
The host suite now checks this API contract. Unpairing locally still requires
forgetting the old accessory on the host before a fresh host-side pairing.
After the identity/lifecycle corrections, Windows recorded removal of the old
pairing at 20:18:56 IST and a successful new pairing at 20:19:11 IST on the same
date; COM7 also reached Connected. Earlier failed attempts recovered after
toggling PC Bluetooth. This is evidence of successful recovery, not proof that
every Windows adapter/cache state is handled. Microsoft's [connection troubleshooting](https://support.microsoft.com/en-us/windows/hardware/bluetooth/troubleshoot-bluetooth-not-connecting-in-windows)
includes removing/re-adding the accessory and toggling Bluetooth.

For a Fire TV test, create a separate BT Keyboard profile (for example Fire TV)
and keep PC Bluetooth off during initial pairing. A still-paired PC profile
accepts only its saved PC peer; it must use Pair new before accepting a TV.

Run the host suite and ESP-IDF build before flashing. On hardware, check:

1. Fresh pair, first key delivery, all twelve mapped short actions, and Power hold.
2. Reboot the remote, reopen its BT tile, and reconnect without pairing again.
3. Cycle MODE repeatedly; confirm the host remains connected and settings keys
   only navigate the OLED.
4. BACK/HOME out of the BT menu/settings; confirm the host disconnects and the
   controller powers down. Reopen and reconnect.
5. Lose the host link while open; check reconnect status and recovery.
6. Cancel/confirm Unpair and Pair new; reconnect with the same and a different host.
7. Let fresh pairing expire and retry. Check a stale host pairing after unpairing.
8. Save two host profiles, alternate them, remove one, and verify the other and all
   IR recordings remain usable.
9. Create each keyboard preset; verify Media volume/mute/playback, a Custom
   Ctrl/Shift/Alt combination, repeated characters and punctuation in Text,
   cancellation with MODE, and retention of assignments after reboot.
