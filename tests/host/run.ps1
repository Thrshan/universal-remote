$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
Push-Location $taskRoot
try {
    New-Item -ItemType Directory -Force build_host | Out-Null
    $taskCommonArgs = @('-std=c11', '-O0', '-g', '-Wall', '-Wextra', '-Wno-unused-parameter')
    $taskIncludes = @(
        '-Itests/host/include', '-Itests/host',
        '-Icomponents/app_core', '-Icomponents/app_core/profiles', '-Icomponents/bluetooth',
        '-Imain/drivers', '-Icomponents/ui/include', '-Icomponents/ui', '-Imanaged_components/u8g2/csrc'
    )
    $taskProfiles = @(
        'components/app_core/profiles/bt_keymap.c',
        'components/app_core/profiles/bt_mouse.c',
        'components/app_core/profiles/bt_output.c'
    )
    $taskCore = @(
        'components/app_core/app_state.c',
        'components/app_core/remote_model.c',
        'components/app_core/app_storage.c'
    ) + $taskProfiles
    $taskUi = @(
        'components/ui/ui_nav.c', 'components/ui/ui_screens.c',
        'components/ui/ui_icons.c', 'components/ui/ui_common.c',
        'components/ui/screens/ui_home.c', 'components/ui/screens/ui_settings.c',
        'components/ui/screens/ui_remote.c', 'components/ui/screens/ui_edit.c',
        'components/ui/screens/ui_wizard.c', 'components/ui/screens/ui_bluetooth.c',
        'components/ui/screens/ui_ir.c'
    )
    function Invoke-HostCheck {
        param([string]$Name, [string[]]$Sources, [string[]]$ExtraArgs = @())
        $taskExecutable = Join-Path 'build_host' ($Name + '.exe')
        & gcc @taskCommonArgs @ExtraArgs @taskIncludes @Sources -o $taskExecutable
        if ($LASTEXITCODE -ne 0) { throw "$Name compilation failed" }
        & (Join-Path $taskRoot $taskExecutable)
        if ($LASTEXITCODE -ne 0) { throw "$Name checks failed" }
    }

    $taskSnapshots = Get-Content -LiteralPath tests/host/fixtures/ui_snapshot_hashes.json | ConvertFrom-Json
    foreach ($taskSnapshot in $taskSnapshots) {
        if ([IO.Path]::GetFileName($taskSnapshot.Name) -ne $taskSnapshot.Name -or
            [IO.Path]::GetExtension($taskSnapshot.Name) -ne '.pbm') {
            throw 'Invalid framebuffer fixture filename'
        }
        $taskFrame = Join-Path 'build_host' $taskSnapshot.Name
        if (Test-Path -LiteralPath $taskFrame) { Remove-Item -LiteralPath $taskFrame }
    }
    $taskUiSources = @(
        'tests/host/test_ui.c', 'tests/host/host_support.c', 'tests/host/bt_fake.c',
        'components/bluetooth/bt_advertising.c'
    ) + $taskCore + $taskUi + @(Get-ChildItem 'managed_components/u8g2/csrc/*.c' | ForEach-Object { $_.FullName })
    Invoke-HostCheck -Name test_ui -Sources $taskUiSources -ExtraArgs @(
        '-ffunction-sections', '-fdata-sections', '-Wno-sign-compare', '-Wl,--gc-sections'
    )
    foreach ($taskSnapshot in $taskSnapshots) {
        $taskHash = Get-FileHash -LiteralPath (Join-Path 'build_host' $taskSnapshot.Name) -Algorithm SHA256
        if ($taskHash.Hash -ne $taskSnapshot.Hash) { throw "Framebuffer changed: $($taskSnapshot.Name)" }
    }
    Write-Output "PASS: $($taskSnapshots.Count) screen renders match original firmware bytes."

    $taskRuntimeArgs = @('-Werror', '-Itests/host/bt_runtime/include', '-Icomponents/esp_hid/include')
    # The runtime harness includes the production worker; only the SDK/RTOS boundaries are substituted.
    Invoke-HostCheck -Name test_bt_runtime -Sources (@('tests/host/bt_runtime/test_bt_runtime.c') + $taskProfiles) -ExtraArgs $taskRuntimeArgs
    Invoke-HostCheck -Name test_bt_mouse -Sources @('tests/host/test_bt_mouse.c', 'components/app_core/profiles/bt_mouse.c') -ExtraArgs @('-Werror')
    Invoke-HostCheck -Name test_ccc_store -Sources @('tests/host/bt_runtime/test_ccc_store.c', 'components/bluetooth/bt_ccc_store.c') -ExtraArgs $taskRuntimeArgs
    Invoke-HostCheck -Name test_storage_compat -Sources (@('tests/host/test_storage_compat.c', 'tests/host/host_support.c') + $taskCore) -ExtraArgs @('-Werror')
} finally {
    Pop-Location
}
