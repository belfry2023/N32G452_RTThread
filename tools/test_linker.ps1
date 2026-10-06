param([string]$Preset = 'rtthread-lvgl')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$previousN32Path = $env:PATH
Push-Location $projectRoot
try {
    $compilerDirectory = Split-Path -Parent (Get-Command arm-none-eabi-gcc.exe).Source
    $env:PATH = $compilerDirectory + ';' + $previousN32Path
    $elf = Join-Path $projectRoot "build/$Preset/n32g452-firmware.elf"
    if (-not (Test-Path -LiteralPath $elf)) { throw 'Build the selected firmware first' }
    $testDirectory = Join-Path $projectRoot 'build/linker-tests'
    New-Item -ItemType Directory -Force $testDirectory | Out-Null
    $common = @("-DOBJDUMP=$compilerDirectory/arm-none-eabi-objdump.exe",
        "-DNM=$compilerDirectory/arm-none-eabi-nm.exe", "-DREADELF=$compilerDirectory/arm-none-eabi-readelf.exe",
        '-DFLASH_ORIGIN=0x08000000', '-DFLASH_LEN=0x80000', '-DRAM_ORIGIN=0x20000000',
        '-DRAM_LEN=0x12000', '-DRAM_TOTAL_LEN=0x24000')
    function Invoke-NativeCapture([string]$Program, [string[]]$CommandArguments) {
        # Windows PowerShell 5 turns redirected stderr into ErrorRecords.
        # Expected negative-test diagnostics must not terminate the script;
        # inspect the exit code and diagnostic text explicitly below.
        $ErrorActionPreference = 'Continue'
        $lines = & $Program @CommandArguments 2>&1
        return @{ Code = $LASTEXITCODE; Lines = $lines }
    }
    function Invoke-ImageCheck([string]$Name, [string]$Path, [string]$Expected, [string[]]$Extra = @()) {
        $captured = Invoke-NativeCapture 'cmake' (@("-DELF=$Path") + $common + $Extra + @('-P', 'cmake/verify_link.cmake'))
        $result = $captured.Lines
        $rc = $captured.Code
        $result | Out-File -Encoding utf8 (Join-Path $testDirectory "$Name.log")
        if ($Expected) {
            if ($rc -eq 0 -or (($result -join "`n") -notmatch $Expected)) { throw "Negative case $Name did not fail as expected" }
            Write-Output "PASS: rejected $Name"
        } elseif ($rc -ne 0) { throw "Control image failed: $result" }
        else { Write-Output 'PASS: valid production ELF' }
    }
    Invoke-ImageCheck 'valid' $elf ''
    $headers = & arm-none-eabi-objdump -h $elf
    if ($LASTEXITCODE -ne 0) { throw 'Cannot read section headers' }
    $vectorHeader = ($headers | Select-String '^\s*\d+\s+\.isr_vector\s+\S+\s+\S+\s+\S+\s+([0-9a-fA-F]+)').Matches
    if ($vectorHeader.Count -ne 1) { throw 'Cannot find vector file offset' }
    $vectorOffset = [Convert]::ToInt32($vectorHeader[0].Groups[1].Value, 16)
    function Test-PatchedWord([string]$Name, [int]$Offset, [uint32]$Value, [string]$Expected) {
        $bytes = [IO.File]::ReadAllBytes($elf)
        [BitConverter]::GetBytes($Value).CopyTo($bytes, $Offset)
        $path = Join-Path $testDirectory "$Name.elf"
        [IO.File]::WriteAllBytes($path, $bytes)
        Invoke-ImageCheck $Name $path $Expected
    }
    $sourceBytes = [IO.File]::ReadAllBytes($elf)
    $reset = [BitConverter]::ToUInt32($sourceBytes, $vectorOffset + 4)
    Test-PatchedWord 'bad-sp' $vectorOffset 0x20024000 'Initial SP'
    Test-PatchedWord 'reset-no-thumb' ($vectorOffset + 4) ($reset - 1) 'vector 1 is not'
    Test-PatchedWord 'reset-wrong-handler' ($vectorOffset + 4) ([BitConverter]::ToUInt32($sourceBytes, $vectorOffset + 8)) 'Reset vector equals'
    Test-PatchedWord 'entry-no-thumb' 24 ($reset - 1) 'Entry equals'
    $missingRti = Join-Path $testDirectory 'missing-rti.elf'
    $captured = Invoke-NativeCapture 'arm-none-eabi-objcopy' @('--remove-section=.rti_fn', $elf, $missingRti)
    $captured.Lines | Out-File -Encoding utf8 (Join-Path $testDirectory 'prepare-missing-rti.log')
    if ($captured.Code -ne 0) { throw 'Cannot prepare missing table case' }
    Invoke-ImageCheck 'missing-rti' $missingRti 'missing (symbol|section)'
    Invoke-ImageCheck 'missing-tool' $elf 'tool failed' @('-DNM=nonexistent-arm-nm-for-negative-test')
    Invoke-ImageCheck 'ram-too-small' $elf 'exceeds physical memory' @('-DRAM_TOTAL_LEN=0x20000')

    # Exercise ld itself, not only the post-link checker. Valid baseline first.
    $ldText = [IO.File]::ReadAllText((Join-Path $projectRoot "build/$Preset/n32g452_rtthread.ld"))
    $cases = @(
        @{Name='ld-control'; Text=$ldText; Expected=''},
        @{Name='ld-short-stack'; Text=($ldText -replace '_Min_Stack_Size\s*=\s*[^;]+;', '_Min_Stack_Size = 0x400;'); Expected='MSP stack must'},
        @{Name='ld-heap-overlap'; Text=($ldText -replace '_Min_Heap_Size\s*=\s*[^;]+;', '_Min_Heap_Size = 0x12000;'); Expected='overlaps MSP stack'}
    )
    foreach ($case in $cases) {
        $scriptPath = Join-Path $testDirectory ($case.Name + '.ld')
        [IO.File]::WriteAllText($scriptPath, $case.Text)
        $captured = Invoke-NativeCapture 'arm-none-eabi-gcc' @('-mcpu=cortex-m4', '-mthumb', '-nostdlib', "-T$scriptPath", 'tests/linker_fixture.s', '-o', (Join-Path $testDirectory ($case.Name + '.elf')))
        $result = $captured.Lines
        $rc = $captured.Code
        $result | Out-File -Encoding utf8 (Join-Path $testDirectory ($case.Name + '.log'))
        if ($case.Expected) {
            if ($rc -eq 0 -or (($result -join "`n") -notmatch $case.Expected)) { throw "Assertion case $($case.Name) did not reject invalid layout" }
        } elseif ($rc -ne 0) { throw "Linker fixture control failed: $result" }
        Write-Output "PASS: $($case.Name)"
    }
    Write-Output 'Linker regression: 2 positive controls and 9 rejected invalid cases passed.'
} finally { $env:PATH = $previousN32Path; Pop-Location }
