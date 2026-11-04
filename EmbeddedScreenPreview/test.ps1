# Build and run the previewer regression suite.
param([switch] $SkipBuild)
$ErrorActionPreference = "Stop"
if (-not $SkipBuild) { & "$PSScriptRoot\build.ps1" }
$previousPath = $env:PATH
$previousTmp = $env:TMP
$previousTemp = $env:TEMP
Push-Location $PSScriptRoot
try {
    $env:PATH = "C:\msys64\mingw64\bin;$env:PATH"
    $env:TMP = "C:\pc_build"
    $env:TEMP = $env:TMP
    function Invoke-Checked([string] $Program, [string[]] $Arguments) {
        & $Program @Arguments
        if ($LASTEXITCODE -ne 0) { throw "$Program failed: $LASTEXITCODE" }
    }
    Invoke-Checked gcc @('-std=c11', '-O2', '-Wall', '-Wextra', '-Wno-cast-function-type', '-o', 'tests_loader.exe', 'tests_loader.c')
    Invoke-Checked .\tests_loader.exe @($PWD.Path)
    Invoke-Checked gcc @('-std=c11', '-O2', '-Wall', '-Wextra', '-o', 'tests_sim.exe', 'tests_sim.c', 'fv_sim.c')
    Invoke-Checked .\tests_sim.exe @()
    $fixture = Join-Path $PSScriptRoot '_fbview/tests'
    $small = Join-Path $fixture 'small'
    $bad = Join-Path $fixture 'bad'
    New-Item -ItemType Directory -Force $small, $bad | Out-Null
    Copy-Item -LiteralPath "$PSScriptRoot/demo_adapter.dll" -Destination "$small/demo_adapter.dll"
    @('width=320', 'height=240', 'fps=15', 'bandwidth_mb_s=12', 'scan_hz=50') | Set-Content -LiteralPath "$small/preview.ini" -Encoding ascii
    Invoke-Checked gcc @('-std=c11', '-O2', '-Wall', '-Wextra', '-Wno-cast-function-type', '-o', 'tests_host.exe', 'tests_host.c', 'fbview_loader.c', 'fv_sim.c', 'fv_terminal.c', '-lgdi32', '-luser32', '-lshell32', '-lole32', '-lcomctl32')
    Invoke-Checked .\tests_host.exe @($PWD.Path, $small, $bad)
    $game = '../STM32F429/Game_LCD'
    Invoke-Checked gcc @('-std=c11', '-O2', '-Wall', '-Wextra', "-I$game/User/game", '-o', 'tests_game.exe', "$game/tests_game_pc.c", "$game/User/game/game.c", '-lm')
    Invoke-Checked .\tests_game.exe @()
    Invoke-Checked gcc @('-std=c11', '-O2', '-Wall', '-Wextra', "-I$game/User/game", '-o', 'tests_app.exe', "$game/tests_app_pc.c", "$game/User/game/game.c", "$game/User/game/app.c", '-lm')
    Invoke-Checked .\tests_app.exe @()
    Invoke-Checked gcc @('-std=c11', '-O2', '-Wall', '-Wextra', '-Wno-cast-function-type', '-DPC_PREVIEW', '-I.', "-I$game/User/game", '-o', 'tests_preview.exe', 'tests_preview.c', 'fbview_loader.c', 'fv_sim.c', 'fv_terminal.c', "$game/User/game/pc_bsp_lcd.c", "$game/User/game/game.c", "$game/User/game/app.c", "$game/User/game/render.c", '-lgdi32', '-luser32', '-lshell32', '-lole32', '-lcomctl32')
    Invoke-Checked .\tests_preview.exe @()
    Invoke-Checked gcc @('-std=c11', '-O2', '-Wall', '-Wextra', '-o', 'tests_terminal.exe', 'tests_terminal.c', '-luser32')
    Invoke-Checked .\tests_terminal.exe @()

    Invoke-Checked .\fbview.exe @('-dir', $PWD.Path, '-export', 'regression.bmp')
    $bmp = [IO.File]::ReadAllBytes("$PWD\regression.bmp")
    if ($bmp.Length -ne 1536054 -or $bmp[0] -ne 66 -or $bmp[1] -ne 77 -or
        [BitConverter]::ToInt32($bmp, 18) -ne 800 -or [BitConverter]::ToInt32($bmp, 22) -ne -480) {
        throw 'Unexpected BMP dimensions or header'
    }
    Invoke-Checked .\fbview.exe @('-dir', $PWD.Path, '-bandwidth', '60', '-export', 'limited.bmp')
    Invoke-Checked .\fbview.exe @('-dir', $PWD.Path, '-bandwidth', '0', '-export', 'recovered.bmp')
    $baseline = (Get-FileHash -LiteralPath 'regression.bmp').Hash
    if ((Get-FileHash -LiteralPath 'limited.bmp').Hash -eq $baseline) { throw 'Bandwidth limit did not change the displayed frame' }
    if ((Get-FileHash -LiteralPath 'recovered.bmp').Hash -ne $baseline) { throw 'Unlimited bandwidth did not restore the frame' }
    # The demo has no white shapes: these pixels prove its text reached the BMP.
    $white = 0
    for ($offset = 54; $offset -lt 54 + 800 * 40 * 4; $offset += 4) {
        if ($bmp[$offset] -eq 255 -and $bmp[$offset + 1] -eq 255 -and $bmp[$offset + 2] -eq 255) { $white++ }
    }
    if ($white -lt 10) { throw 'Text is missing from the exported display frame' }
    Invoke-Checked .\fbview.exe @('-dir', "$game", '-export', 'game-regression.bmp')
    Write-Host 'All regression checks passed.' -ForegroundColor Green
}
finally {
    Pop-Location
    $env:PATH = $previousPath
    $env:TMP = $previousTmp
    $env:TEMP = $previousTemp
}
