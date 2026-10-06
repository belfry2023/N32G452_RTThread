$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$requiredTools = @('cmake.exe','ninja.exe','arm-none-eabi-gcc.exe',
    'arm-none-eabi-objcopy.exe','arm-none-eabi-size.exe',
    'arm-none-eabi-objdump.exe','arm-none-eabi-nm.exe','arm-none-eabi-readelf.exe')
foreach ($name in $requiredTools) {
    $resolved = (Get-Command $name -ErrorAction Stop).Source
    Write-Output ($name + ': ' + $resolved)
}
$requiredFiles = @('Nations.N32G45x_Library.2.6.0/firmware/CMSIS/core/core_cm4.h',
    'third_party/lvgl/lvgl.h','inc/board_config.h','CMakePresets.json')
foreach ($relative in $requiredFiles) {
    if (!(Test-Path -LiteralPath (Join-Path $projectRoot $relative))) {
        throw ('Required project file missing: ' + $relative)
    }
}
& cmake --version | Select-Object -First 1
& ninja --version
& arm-none-eabi-gcc -dumpfullversion
Write-Output 'PASS: required build tools and project dependencies found (no global settings changed).'
