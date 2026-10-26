$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
Push-Location $taskRoot
try {
    New-Item -ItemType Directory -Force build_host | Out-Null
    $sources = @(
        'tests/host/test_ui.c', 'tests/host/host_support.c', 'main/app_state.c',
        'components/ui/ui_nav.c', 'components/ui/ui_screens.c', 'components/ui/ui_icons.c'
    ) + @(Get-ChildItem 'managed_components/u8g2/csrc/*.c' | ForEach-Object { $_.FullName })
    & gcc -std=c11 -O0 -g -ffunction-sections -fdata-sections -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Itests/host/include -Itests/host -Imain -Icomponents/ui/include -Imanaged_components/u8g2/csrc @sources '-Wl,--gc-sections' -o build_host/test_ui.exe
    if ($LASTEXITCODE -ne 0) { throw 'Host compilation failed' }
    & ./build_host/test_ui.exe
    if ($LASTEXITCODE -ne 0) { throw 'Behavior checks failed' }
} finally {
    Pop-Location
}
