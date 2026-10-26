# Remote keypad

The keypad uses the eight-node contact scanner from
`matrix_keyboard/main/matrix_keyboard_example_main.c`, replacing the old
row/column scanner. Key numbers match the numbered PCB photo.

## Wiring

Connect the nodes in the same order as the working keypad example:

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

The OLED uses SDA GPIO 8 and SCL GPIO 9. Its reset pin is disabled because the
display only has power, ground, SDA, and SCL. GPIO 10 is reserved for keypad
node 6. GPIO 12 is no longer used by the keypad.

## UI controls

| PCB key | Contact nodes | Action |
| --- | --- | --- |
| 5 | 2, 6 | Up |
| 6 | 3, 6 | Left |
| 7 | 5, 6 | OK / select |
| 8 | 4, 6 | Right |
| 9 | 2, 7 | Down |
| 10 | 3, 7 | Back |

All 21 contacts retain the example's key numbers and press/release logs. Only
the six keys above send UI navigation events. The node 7/8 contact remains
ignored, as in the example. The scanner reports one contact at a time.

The scanner requires five matching samples to debounce presses and releases.
An unstable scan leaves the previous key state intact. Holding an arrow starts
scrolling after 350 ms and repeats every 120 ms, subject to scan timing. OK and
Back do not repeat; they retain the existing long-press event types at 700 ms.

## Check on hardware

Build and flash Universal Remote using the ESP-IDF 5.5 environment. On the home
screen, check all four directions, press key 7 to open the selected item, and
press key 10 to return. Hold an arrow to check repeat scrolling, then release
it to check that scrolling stops. Other keys should log their key numbers
without navigating the UI.
