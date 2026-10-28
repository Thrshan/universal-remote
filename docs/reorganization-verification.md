# Source reorganization verification

Date: 2026-10-28. Baseline: Git revision
`c428302ce515e95a14643871ce2b7fbb59bf2682`, with a clean working tree before work.

The work was applied in order: remove inactive experiments; group sources and
remove the component cycle; extract model/events/storage; split UI screens;
add compatibility fixtures and update documentation. The existing four host
suites were run before changes and after each of the first four stages. Every
run retained the same passing check counts. All 45 framebuffer files were also
compared with the baseline after each stage, with exact matches.

## Final automated checks

Run `tests/host/run.ps1` from a checkout with Windows GCC and the u8g2 dependency.
The runner returns a failure if compilation, any behavior check, any expected
frame generation, any framebuffer hash, or any stored-byte comparison fails.

| Check | Baseline | Final |
| --- | ---: | ---: |
| UI, profile storage, IR workflow and keyboard mapping checks | 994 | 994 passing |
| Production Bluetooth worker with deterministic SDK/RTOS substitutes | 482 | 482 passing |
| Mouse clicks, drag, wheels, patterns, speed and motion | 12,018 | 12,018 passing |
| Bonded notification-subscription storage | 31 | 31 passing |
| Exact screen renders | 45 captured | 45 byte-identical |
| Additional storage compatibility checks | Original fixtures captured | 88 passing |

The final run passes 13,613 behavioral checks plus the 45 framebuffer
comparisons. The storage compatibility suite compares six snapshots against
five binary fixtures produced by compiling the original sources. It covers
mixed transports and presets, text, custom modifiers, mouse settings, saved
identities/peers, edits/reordering, failed saves, and both older profile formats
and all 32 legacy IR recordings. Fixture provenance is documented in
`tests/host/fixtures/README.md`.

The updated typography-preview script also compiled and ran successfully using
Python with Pillow. Generated preview files were kept in the ignored build
directory; the checked-in UI artwork was retained.

## Source and dependency audit

A one-time comparison with the baseline verified the original bodies of all
26 model/state/storage functions, 68 UI-screen/helper functions, and 42 Bluetooth
runtime functions. The comparison accounts for helper/API renames and the
explicit storage-context parameter. It also verified the active code in 11
other moved/updated source files, ignoring comments/includes and excluding the
unused OLED demonstration that was removed. No remote-control algorithm, task
timing, pin assignment, report map, or persistent model declaration was changed.

The generated ESP-IDF component dependency graph was checked for cycles:
`main` depends on UI, Bluetooth and the shared core; UI depends on Bluetooth and
the shared core; Bluetooth depends on the shared core and the existing HID
component. The shared core has no project-component dependency.

## Firmware builds and memory

ESP-IDF 5.5.2 builds for ESP32-S3 succeeded at baseline, after source grouping,
after model/storage extraction, after the UI split, and after final driver
cleanup. The final build completed without compiler warnings or errors.

| Measure | Baseline | Final |
| --- | ---: | ---: |
| Application binary | 863,408 bytes | 863,616 bytes |
| Initialized DRAM (`.dram0.data`) | 16,560 bytes | 16,560 bytes |
| Uninitialized DRAM (`.dram0.bss`) | 42,640 bytes | 42,632 bytes |
| IRAM code (`.iram0.text`) | 96,615 bytes | 96,615 bytes |

The final application has 44% of its configured partition free. File/function
reorganization changes the built binary; it is not expected to have the same
binary hash. The saved profile structure remains 104 bytes with IR data at
offset 20, enforced by the existing compile-time assertion.

Local execution records are under `build_host/organization-*.log`, with source
and dependency/memory audit results in `build_host/organization-*-audit.json`
and `build_host/organization-source-audit.json`.

## Device validation still required

The remote was not flashed as part of this work. Automated checks establish
that the exercised behavior and saved bytes are preserved; they do not emulate
electrical IR transmission or real Bluetooth radio/host behavior.

After flashing through the normal ESP-IDF workflow, repeat existing-remote IR
replay and learning; saved Bluetooth reconnection; keyboard/text output; mapped
mouse drag and pattern controls; switching between saved keyboard/mouse remotes;
and reboot persistence. Use the detailed hardware checklist in
[the Bluetooth guide](bluetooth.md). Existing recordings and bonds should be
retained; no erase or storage migration change is part of this refactor.
