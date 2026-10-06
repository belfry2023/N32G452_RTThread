param(
    [ValidateSet('All','AC5_Headless','AC5_LVGL','AC6_Headless','AC6_LVGL')]
    [string]$Target = 'All',
    [string]$KeilRoot = (Join-Path $env:LOCALAPPDATA 'Keil_v5')
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$project = Join-Path $projectRoot 'keil\official-pack\N32G452_OfficialPack_RTThread.uvprojx'
$logRoot = Join-Path $projectRoot 'build\official-pack-check\full'
$uv4 = Join-Path $KeilRoot 'UV4\UV4.exe'
if (-not (Test-Path -LiteralPath $project)) { throw 'Run tools/prepare_keil_official_pack.py first.' }
New-Item -ItemType Directory -Force $logRoot | Out-Null
$targets = if ($Target -eq 'All') { @('AC5_Headless','AC5_LVGL','AC6_Headless','AC6_LVGL') } else { @($Target) }
$results = @()
foreach ($name in $targets) {
    $log = Join-Path $logRoot ($name + '.log')
    if (Test-Path -LiteralPath $log) { Remove-Item -LiteralPath $log }
    $arguments = '-r "{0}" -t "{1}" -j0 -o "{2}"' -f $project, $name, $log
    $process = Start-Process -FilePath $uv4 -ArgumentList $arguments -WindowStyle Hidden -PassThru
    $process.WaitForExit()
    if (-not (Test-Path -LiteralPath $log)) { throw "No build log: $name" }
    $summaries = [regex]::Matches((Get-Content -LiteralPath $log -Raw), '(\d+) Error\(s\), (\d+) Warning\(s\)')
    if ($process.ExitCode -gt 1 -or $summaries.Count -eq 0) { throw "Keil failed: $name. See $log" }
    $last = $summaries[$summaries.Count - 1]
    $errors = [int]$last.Groups[1].Value
    $warnings = [int]$last.Groups[2].Value
    if ($errors -ne 0) { throw "Build errors: $log" }
    $image = Join-Path (Split-Path $project -Parent) ('Objects\' + $name + '\n32g452_rtthread.axf')
    $compiler = if ($name.StartsWith('AC5')) { 'ARMCC' } else { 'ARMCLANG' }
    $fromelf = Join-Path $KeilRoot ('ARM\' + $compiler + '\bin\fromelf.exe')
    & $fromelf --bin --output ([IO.Path]::ChangeExtension($image, '.bin')) $image
    if ($LASTEXITCODE -ne 0) { throw "BIN conversion failed: $image" }
    $results += [PSCustomObject]@{target=$name;errors=$errors;warnings=$warnings;image=$image;log=$log;sha256=(Get-FileHash -LiteralPath $image -Algorithm SHA256).Hash}
    Write-Output ("{0}: {1} errors, {2} warnings; AXF/HEX/BIN generated" -f $name,$errors,$warnings)
}
$results | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $logRoot 'results.json') -Encoding UTF8
