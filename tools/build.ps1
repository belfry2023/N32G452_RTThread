param(
    [ValidateSet('rtthread-lvgl','rtthread-lvgl-release','rtthread')]
    [string]$Preset = 'rtthread-lvgl'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot
$previousN32Path = $env:PATH
try {
    $compilerDirectory = Split-Path -Parent (Get-Command arm-none-eabi-gcc.exe).Source
    $env:PATH = $compilerDirectory + ';' + $previousN32Path
    & cmake --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed' }
    & cmake --build --preset $Preset --parallel 6
    if ($LASTEXITCODE -ne 0) { throw 'Firmware build failed' }
    & cmake --build --preset $Preset --target verify-ld
    if ($LASTEXITCODE -ne 0) { throw 'Firmware link validation failed' }
} finally { $env:PATH = $previousN32Path; Pop-Location }
