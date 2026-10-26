# Keypad and portrait UI

The eight-node contact scanner and GPIO wiring are unchanged. Key numbers match
the numbered PCB photograph. All 21 key IDs now reach the UI.

## Wiring

| Node | ESP32 GPIO |
| --- | --- |
| 1 | 4 |
| 2 | 7 |
| 3 | 15 |
| 4 | 16 |
| 5 | 17 |
| 6 | 10 |
| 7 | 11 |
| 8 | 6 |

OLED: SH1106, SDA 8, SCL 9, address 0x3C; no reset GPIO. The physical 128x64 OLED
is rotated clockwise with U8G2_R1, giving a logical **64x128** portrait canvas.
Its controller setup includes the two-column RAM offset used by the working
OLED examples, preventing the two stray rows in portrait orientation.

## Key assignments

| PCB key | Contact nodes | Operation |
| --- | --- | --- |
| 1 | 2, 3 | POWER |
| 2 | 2, 5 | MODE BTN |
| 3 | 3, 5 | CUST BTN 2 |
| 4 | 4, 5 | CUST BTN 3 |
| 5 | 2, 6 | UP |
| 6 | 3, 6 | LEFT |
| 7 | 5, 6 | OK |
| 8 | 4, 6 | RIGHT |
| 9 | 2, 7 | DOWN |
| 10 | 3, 7 | BACK |
| 11 | 4, 7 | PLAY_PAUSE |
| 12 | 5, 7 | HOME |
| 13 | 2, 8 | VOL UP |
| 14 | 5, 8 | CH UP |
| 15 | 3, 8 | VOL CLICK |
| 16 | 4, 8 | CH CLICK |
| 17 | 1, 3 | VOL DOWN |
| 18 | 1, 5 | CH DOWN |
| 19 | 1, 2 | CUST BTN 4 |
| 20 | 1, 6 | CUST BTN 5 |
| 21 | 1, 4 | CUST BTN 6 |

MODE is reserved for switching between appliance operation and browsing.
The other 20 keys can each hold one IR command per remote. POWER is the
appliance power command in Remote mode; this change does not add an electronic
power switch for the ESP32.

The scanner debounces using five matching samples, reports one contact at a
time, and ignores the node 7/8 contact. Arrow, volume up/down and channel up/down
keys repeat after 350 ms, then every 120 ms (subject to scan timing).
OK/BACK/HOME generate long events after 700 ms. Repeat events never select a
new IR learning target.

## Hardware check

1. Build and flash using ESP-IDF 5.5.2 for ESP32-S3.
2. Check portrait orientation and the two-second UNIVERSAL REMOTE startup.
3. Add an IR remote; verify arrows, custom-key keyboard switching and OK.
4. Confirm HOME before icon selection leaves no new remote.
5. Choose an icon; confirm the new remote remains after restarting, even with
   zero learned keys.
6. Learn POWER, BACK and HOME. Short BACK selects BACK for learning; hold BACK
   for 700 ms to return to the previous screen. While waiting for a command,
   short BACK does not cancel; holding BACK cancels. Short HOME selects HOME;
   hold HOME to finish setup.
7. Re-record a learned key, cancel, and verify its original command still sends.
8. MODE enters the highlighted remote; all 20 mapped keys control the appliance.
   MODE returns to browsing. BACK and HOME in Remote mode send appliance commands.
9. Verify IR reception, actual appliance transmission, button repeat behavior
   and saving across a power cycle on the physical hardware.

See [the UI guide](ui.md) for all screens and persistence behavior.
