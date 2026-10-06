$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot
$previousN32Path = $env:PATH
try {
    # cc1 loads DLLs through PATH. Keep MSYS2 DLLs ahead of Anaconda for this
    # process only; restoring PATH leaves other development environments alone.
    $compilerDirectory = Split-Path -Parent (Get-Command gcc.exe).Source
    $env:PATH = $compilerDirectory + ';' + $previousN32Path
    New-Item -ItemType Directory -Force build/driver-tests | Out-Null
    & gcc -std=c11 -Wall -Wextra -Werror -Iinc tests/protocol_test.c board/gp21_protocol.c -o build/driver-tests/protocol.exe
    if ($LASTEXITCODE -ne 0) { throw 'Protocol test compilation failed' }
    & ./build/driver-tests/protocol.exe
    if ($LASTEXITCODE -ne 0) { throw 'Protocol tests failed' }
    & gcc -std=c11 -Wall -Wextra -Werror -Itests/stubs -Iinc tests/gp21_device_test.c board/gp21_protocol.c -o build/driver-tests/gp21-device.exe
    if ($LASTEXITCODE -ne 0) { throw 'GP21 device test compilation failed' }
    & ./build/driver-tests/gp21-device.exe
    if ($LASTEXITCODE -ne 0) { throw 'GP21 device tests failed' }
    & gcc -std=c11 -Wall -Wextra -Werror -DN32_USE_RTTHREAD -DN32_LCD_TRACE_TEST -Itests/stubs -Iinc -Iboard tests/lcd_test.c board/drv_lcd.c -o build/driver-tests/lcd.exe
    if ($LASTEXITCODE -ne 0) { throw 'LCD test compilation failed' }
    & ./build/driver-tests/lcd.exe
    if ($LASTEXITCODE -ne 0) { throw 'LCD trace tests failed' }
    & gcc -std=c11 -Wall -Wextra -Werror -Iboard tools/key_gesture_test.c board/key_gesture.c -o build/driver-tests/keys.exe
    if ($LASTEXITCODE -ne 0) { throw 'Key test compilation failed' }
    & ./build/driver-tests/keys.exe
    if ($LASTEXITCODE -ne 0) { throw 'Key tests failed' }
} finally { $env:PATH = $previousN32Path; Pop-Location }
