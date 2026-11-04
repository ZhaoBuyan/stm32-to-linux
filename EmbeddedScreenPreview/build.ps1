# EmbeddedScreenPreview 构建脚本
#
# 构建两个东西：
#   1. fbview.exe         —— 模拟器本体（通用，只需编一次）
#   2. demo_adapter.dll   —— 自带的示例适配器（验证架构能跑）
#
# 用法：
#   .\build.ps1              两个都编
#   .\build.ps1 -HostOnly    只编模拟器

param(
    [switch] $HostOnly
)

$ErrorActionPreference = "Stop"

# 中文路径会让 gcc 建临时文件失败
$env:TMP  = "C:\pc_build"
$env:TEMP = "C:\pc_build"
New-Item -ItemType Directory -Force -Path $env:TMP | Out-Null

$GCC = "C:\msys64\mingw64\bin\gcc.exe"
if (-not (Test-Path $GCC)) { throw "找不到 gcc: $GCC" }

Push-Location $PSScriptRoot
try {
    Write-Host "[1/2] 编译 fbview.exe ..." -ForegroundColor Cyan
    # -Wno-cast-function-type: GetProcAddress 返回 FARPROC，转成具体函数指针必须强转，无实际风险
    & $GCC -std=c11 -O2 -Wall -Wextra -Wno-cast-function-type -I. -o fbview.exe `
        fbview_main.c fbview.c fbview_loader.c `
        -lgdi32 -luser32 -lshell32 -lole32
    if ($LASTEXITCODE -ne 0) { throw "fbview.exe 编译失败" }
    Write-Host "      OK -> fbview.exe" -ForegroundColor Green

    if (-not $HostOnly) {
        Write-Host "[2/2] 编译 demo_adapter.dll ..." -ForegroundColor Cyan
        & $GCC -std=c11 -O2 -Wall -Wextra -shared -DFBVIEW_BUILD_DLL -I. `
            -o demo_adapter.dll demo_adapter.c
        if ($LASTEXITCODE -ne 0) { throw "demo_adapter.dll 编译失败" }
        Write-Host "      OK -> demo_adapter.dll" -ForegroundColor Green
    }
}
finally {
    Pop-Location
}