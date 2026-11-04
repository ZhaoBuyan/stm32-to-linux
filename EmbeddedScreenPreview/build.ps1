# EmbeddedScreenPreview 构建脚本
#
# 构建宿主与示例/游戏适配器：
#   1. fbview.exe         —— 模拟器本体（通用，只需编一次）
#   2. demo_adapter.dll   —— 自带的示例适配器（验证架构能跑）
#   3. game_adapter.dll   —— 仓库内 Game_LCD 的 PC 适配器（工程存在时）
#
# 用法：
#   .\build.ps1              全部编译
#   .\build.ps1 -HostOnly    只编模拟器

param(
    [switch] $HostOnly
)

$ErrorActionPreference = "Stop"

# gcc 的子进程（cc1/as/ld）也需要找到 MinGW 运行库。
$oldPath = $env:PATH
$oldTmp = $env:TMP
$oldTemp = $env:TEMP
$env:PATH = "C:\msys64\mingw64\bin;$env:PATH"

# 中文路径会让 gcc 建临时文件失败
$env:TMP  = "C:\pc_build"
$env:TEMP = "C:\pc_build"
New-Item -ItemType Directory -Force -Path $env:TMP | Out-Null

$GCC = "C:\msys64\mingw64\bin\gcc.exe"
if (-not (Test-Path $GCC)) { throw "找不到 gcc: $GCC" }

Push-Location $PSScriptRoot
try {
    Write-Host "[1/3] 编译 fbview.exe ..." -ForegroundColor Cyan
    # -Wno-cast-function-type: GetProcAddress 返回 FARPROC，转成具体函数指针必须强转，无实际风险
    & $GCC -std=c11 -O2 -Wall -Wextra -Wno-cast-function-type -I. -o fbview.exe `
        fbview_main.c fbview.c fbview_loader.c fv_sim.c fv_terminal.c `
        -lgdi32 -luser32 -lshell32 -lole32 -lcomctl32
    if ($LASTEXITCODE -ne 0) { throw "fbview.exe 编译失败" }
    Write-Host "      OK -> fbview.exe" -ForegroundColor Green

    if (-not $HostOnly) {
        Write-Host "[2/3] 编译 demo_adapter.dll ..." -ForegroundColor Cyan
        & $GCC -std=c11 -O2 -Wall -Wextra -shared -DFBVIEW_BUILD_DLL -I. `
            -o demo_adapter.dll demo_adapter.c
        if ($LASTEXITCODE -ne 0) { throw "demo_adapter.dll 编译失败" }
        Write-Host "      OK -> demo_adapter.dll" -ForegroundColor Green
        $gameDir = "../STM32F429/Game_LCD"
        if (Test-Path "$gameDir/User/game/preview_adapter.c") {
            Write-Host "[3/3] 编译 game_adapter.dll ..." -ForegroundColor Cyan
            & $GCC -std=c11 -O2 -Wall -Wextra -shared -DFBVIEW_BUILD_DLL -DPC_PREVIEW -I. "-I$gameDir/User/game" `
                -o "$gameDir/game_adapter.dll" "$gameDir/User/game/preview_adapter.c" `
                "$gameDir/User/game/pc_bsp_lcd.c" "$gameDir/User/game/game.c" `
                "$gameDir/User/game/app.c" "$gameDir/User/game/render.c"
            if ($LASTEXITCODE -ne 0) { throw "game_adapter.dll 编译失败" }
            Write-Host "      OK -> game_adapter.dll" -ForegroundColor Green
        }
    }
}
finally {
    Pop-Location
    $env:PATH = $oldPath
    $env:TMP = $oldTmp
    $env:TEMP = $oldTemp
}