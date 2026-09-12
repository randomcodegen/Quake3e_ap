[CmdletBinding()]
param(
    [string]$Client = "$PSScriptRoot/../build-ap-reuse/Release/quake3e.x64.exe",
    [string]$GameRoot = "$PSScriptRoot/../smoke-ap"
)

$ErrorActionPreference = 'Stop'
$Client = (Resolve-Path -LiteralPath $Client).Path
$GameRoot = (Resolve-Path -LiteralPath $GameRoot).Path
$testRoot = Join-Path $PSScriptRoot "../build-ap-reuse/warmup-test-$([guid]::NewGuid())"
New-Item -ItemType Directory -Path "$testRoot/cpma-ap" -Force | Out-Null
$testRoot = (Resolve-Path -LiteralPath $testRoot).Path
@'
callvote warmup 10 0
ap_cpma_stage_limits
quit
'@ | Set-Content -LiteralPath "$testRoot/cpma-ap/check.cfg"

$arguments = @('+set', 'dedicated', '1', '+set', 'fs_basepath', "`"$GameRoot`"",
    '+set', 'fs_homepath', "`"$testRoot`"", '+set', 'fs_basegame', 'baseq3/cpma',
    '+set', 'fs_game', 'cpma-ap', '+set', 'net_port', '27963', '+set', 'sv_pure', '0',
    '+set', 'logfile', '2', '+set', 'mode_start', '1v1', '+map', 'cpm15', '+exec', 'check.cfg')
$process = Start-Process -FilePath $Client -ArgumentList $arguments -WindowStyle Hidden -PassThru
if (-not $process.WaitForExit(20000)) {
    Stop-Process -Id $process.Id
    throw "CPMA test timed out. Logs: $testRoot"
}
$log = Get-Content -LiteralPath "$testRoot/cpma-ap/qconsole.log" -Raw
if ($process.ExitCode -ne 0 -or $log -notmatch 'Warmup: \^510' -or
    $log -notmatch 'Warmup: \^5DISABLED' -or $log -notmatch 'CPMA stage limits applied') {
    throw "CPMA did not disable warmup. Logs: $testRoot"
}
Write-Output "PASS: cpm15 warmup disabled by AP stage setup. Logs: $testRoot"
