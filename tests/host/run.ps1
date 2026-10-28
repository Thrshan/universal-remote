$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
Push-Location $taskRoot
try {
    New-Item -ItemType Directory -Force build_host | Out-Null
    $sources = @(
        'tests/host/test_ui.c', 'tests/host/host_support.c', 'tests/host/bt_fake.c', 'main/app_state.c', 'main/bt_keymap.c', 'main/bt_mouse.c', 'main/bt_output.c', 'main/bt_advertising.c',
        'components/ui/ui_nav.c', 'components/ui/ui_screens.c', 'components/ui/ui_icons.c'
    ) + @(Get-ChildItem 'managed_components/u8g2/csrc/*.c' | ForEach-Object { $_.FullName })
    & gcc -std=c11 -O0 -g -ffunction-sections -fdata-sections -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Itests/host/include -Itests/host -Imain -Icomponents/ui/include -Imanaged_components/u8g2/csrc @sources '-Wl,--gc-sections' -o build_host/test_ui.exe
    if ($LASTEXITCODE -ne 0) { throw 'Host compilation failed' }
    & ./build_host/test_ui.exe
    if ($LASTEXITCODE -ne 0) { throw 'Behavior checks failed' }
    # Exercise the production Bluetooth worker independently of the UI's bt_fake.c.
    & gcc -std=c11 -O0 -g -Wall -Wextra -Werror -Wno-unused-parameter -Itests/host/bt_runtime/include -Itests/host/include -Icomponents/esp_hid/include -Imain tests/host/bt_runtime/test_bt_runtime.c main/bt_keymap.c main/bt_mouse.c main/bt_output.c -o build_host/test_bt_runtime.exe
    if ($LASTEXITCODE -ne 0) { throw 'Bluetooth worker compilation failed' }
    & ./build_host/test_bt_runtime.exe
    if ($LASTEXITCODE -ne 0) { throw 'Bluetooth worker checks failed' }
    & gcc -std=c11 -O0 -g -Wall -Wextra -Werror -Wno-unused-parameter -Itests/host/include -Imain tests/host/test_bt_mouse.c main/bt_mouse.c -o build_host/test_bt_mouse.exe
    if ($LASTEXITCODE -ne 0) { throw 'Mouse compilation failed' }
    & ./build_host/test_bt_mouse.exe
    if ($LASTEXITCODE -ne 0) { throw 'Mouse checks failed' }
    & gcc -std=c11 -O0 -g -Wall -Wextra -Werror -Wno-unused-parameter -Itests/host/bt_runtime/include -Itests/host/include -Icomponents/esp_hid/include -Imain tests/host/bt_runtime/test_ccc_store.c main/bt_ccc_store.c -o build_host/test_ccc_store.exe
    if ($LASTEXITCODE -ne 0) { throw 'CCC storage compilation failed' }
    & ./build_host/test_ccc_store.exe
    if ($LASTEXITCODE -ne 0) { throw 'CCC storage checks failed' }
} finally {
    Pop-Location
}
