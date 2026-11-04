# PC 预览器构建脚本
#
# 用法：  cd STM32F429\Game_PC ;  .\build.ps1
#         .\game_preview.exe
#
# 前置：C:\msys64\mingw64\bin\gcc.exe 存在（本机是 gcc 16.1.0）

$ErrorActionPreference = "Stop"

# ⚠️ 中文路径会让 gcc 建临时文件失败，必须指到纯英文目录
$env:TMP  = "C:\pc_build"
$env:TEMP = "C:\pc_build"
New-Item -ItemType Directory -Force -Path $env:TMP | Out-Null

$GCC  = "C:\msys64\mingw64\bin\gcc.exe"
$GAME = "..\Game_LCD\User\game"

if (-not (Test-Path $GCC)) { throw "找不到 gcc: $GCC" }

Write-Host "编译中..." -ForegroundColor Cyan

# -DPC_PREVIEW       让 render.c 用 pc_bsp_lcd.h 而不是野火驱动
# -mwindows          不弹控制台窗口（调试期可以先去掉，就能看到 printf）
& $GCC -std=c11 -O2 -Wall -Wextra `
    -DPC_PREVIEW `
    -I. `
    -o game_preview.exe `
    main_pc.c pc_bsp_lcd.c `
    "$GAME\game.c" "$GAME\app.c" "$GAME\render.c" `
    -lgdi32 -luser32 -mwindows

if ($LASTEXITCODE -ne 0) { throw "构建失败（退出码 $LASTEXITCODE）" }

Write-Host "OK -> game_preview.exe" -ForegroundColor Green
Write-Host "直接运行 .\game_preview.exe" -ForegroundColor Green
