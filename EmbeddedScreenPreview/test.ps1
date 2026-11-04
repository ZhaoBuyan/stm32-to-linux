# Build and run the previewer regression suite.
$ErrorActionPreference = "Stop"
& "$PSScriptRoot\build.ps1"
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
    $game = '../STM32F429/Game_LCD'
    Invoke-Checked gcc @('-std=c11', '-O2', '-Wall', '-Wextra', "-I$game/User/game", '-o', 'tests_game.exe', "$game/tests_game_pc.c", "$game/User/game/game.c", '-lm')
    Invoke-Checked .\tests_game.exe @()
    Invoke-Checked gcc @('-std=c11', '-O2', '-Wall', '-Wextra', "-I$game/User/game", '-o', 'tests_app.exe', "$game/tests_app_pc.c", "$game/User/game/game.c", "$game/User/game/app.c", '-lm')
    Invoke-Checked .\tests_app.exe @()
    Invoke-Checked .\fbview.exe @('-dir', $PWD.Path, '-export', 'regression.bmp')
    $bmp = [IO.File]::ReadAllBytes("$PWD\regression.bmp")
    if ($bmp.Length -ne 1152054 -or $bmp[0] -ne 66 -or $bmp[1] -ne 77 -or
        [BitConverter]::ToInt32($bmp, 18) -ne 800 -or [BitConverter]::ToInt32($bmp, 22) -ne 480) {
        throw 'Unexpected BMP dimensions or header'
    }
    Write-Host 'All regression checks passed.' -ForegroundColor Green
}
finally {
    Pop-Location
    $env:PATH = $previousPath
    $env:TMP = $previousTmp
    $env:TEMP = $previousTemp
}
