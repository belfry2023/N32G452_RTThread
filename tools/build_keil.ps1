param(
    [ValidateSet('All','AC5_Headless','AC5_LVGL','AC6_Headless','AC6_LVGL')]
    [string]$Target = 'All',
    [string]$KeilRoot = (Join-Path $env:LOCALAPPDATA 'Keil_v5'),
    [switch]$PackExample
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$uv4 = Join-Path $KeilRoot 'UV4\UV4.exe'
if (-not (Test-Path -LiteralPath $uv4)) { throw "Keil not found: $uv4. Supply -KeilRoot." }
$project = Join-Path $projectRoot 'keil\N32G452_RTThread.uvprojx'
$logRoot = Join-Path $projectRoot 'build\keil-check'
if ($PackExample) {
    $project = Join-Path $projectRoot 'build\pack-stage\Examples\N32G452_RTThread\N32G452_RTThread.uvprojx'
    $logRoot = Join-Path $logRoot 'pack-example'
}
if (-not (Test-Path -LiteralPath $project)) { throw 'Generate the project with python tools/build_pack.py first.' }
New-Item -ItemType Directory -Force $logRoot | Out-Null
$targets = @('AC5_Headless','AC5_LVGL','AC6_Headless','AC6_LVGL')
if ($Target -ne 'All') { $targets = @($Target) }
$results = @()
foreach ($name in $targets) {
    $log = Join-Path $logRoot ($name + '.log')
    # Delete only the previous generated log, so a failed launch cannot pass
    # by matching an old successful build summary.
    if (Test-Path -LiteralPath $log) { Remove-Item -LiteralPath $log }
    $arguments = '-r "{0}" -t "{1}" -j0 -o "{2}"' -f $project, $name, $log
    $process = Start-Process -FilePath $uv4 -ArgumentList $arguments -WindowStyle Hidden -PassThru
    $process.WaitForExit()
    $text = Get-Content -LiteralPath $log -Raw
    $summaries = [regex]::Matches($text, '(\d+) Error\(s\), (\d+) Warning\(s\)')
    if ($process.ExitCode -gt 1 -or $summaries.Count -eq 0) { throw "Keil failed: $name. See $log" }
    $summary = $summaries[$summaries.Count - 1]
    $errors = [int]$summary.Groups[1].Value
    $warnings = [int]$summary.Groups[2].Value
    if ($errors -ne 0) { throw "Keil reported $errors errors: $log" }
    $image = Join-Path $projectRoot ('build\keil\' + $name + '\Objects\n32g452_rtthread.axf')
    if ($PackExample) {
        $image = Join-Path (Split-Path $project -Parent) ('Objects\' + $name + '\n32g452_rtthread.axf')
    }
    if (-not (Test-Path -LiteralPath $image)) { throw "Image not generated: $image" }
    $compiler = if ($name.StartsWith('AC5')) { 'ARMCC' } else { 'ARMCLANG' }
    $fromelf = Join-Path $KeilRoot ('ARM\' + $compiler + '\bin\fromelf.exe')
    $binary = [IO.Path]::ChangeExtension($image, '.bin')
    & $fromelf --bin --output $binary $image
    if ($LASTEXITCODE -ne 0) { throw "BIN conversion failed: $image" }
    $results += [PSCustomObject]@{target=$name; errors=$errors; warnings=$warnings; image=$image; log=$log; sha256=(Get-FileHash -LiteralPath $image -Algorithm SHA256).Hash}
    Write-Output ("{0}: {1} errors, {2} warnings; AXF/HEX/BIN generated" -f $name,$errors,$warnings)
}
$results | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $logRoot 'results.json') -Encoding UTF8
