[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$gameRoot = Join-Path $project 'smoke-ap'
$testHome = Join-Path $project ('build-ap-reuse\cpma-limits-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testHome | Out-Null
$arguments = @(
    '+set dedicated 1', '+set net_ip 127.0.0.1', '+set net_port 27989',
    "+set fs_homepath `"$testHome`"", '+set fs_basegame baseq3/cpma',
    '+set fs_game cpma-ap', '+set vm_game 2', '+set sv_pure 0',
    '+set logfile 2', '+set server_useMapModes 0', '+set mode_start ffa',
    '+set fraglimit 10', '+map cpm22', '+ap_cpma_stage_limits', '+quit'
)
$executable = Join-Path $gameRoot 'quake3e.x64.next.exe'
if (-not (Test-Path -LiteralPath $executable)) { $executable = Join-Path $gameRoot 'quake3e.x64.exe' }
$process = Start-Process -FilePath $executable `
    -WorkingDirectory $gameRoot -ArgumentList $arguments -WindowStyle Hidden -PassThru
if (-not $process.WaitForExit(30000)) {
    Stop-Process -Id $process.Id
    throw "Headless CPMA test timed out. Logs: $testHome"
}
$log = Join-Path $testHome 'cpma-ap\qconsole.log'
$text = Get-Content -LiteralPath $log -Raw
if ($text -notmatch 'Fraglimit: \^510\\n' -or $text -notmatch 'Timelimit: \^50\\n' -or
    $text -match 'Invalid vote|Unknown command|rejected the stage') {
    throw "CPMA did not accept the stage limits. Log: $log"
}
Write-Output 'PASS: CPMA applied score limit 10 and time limit 0.'
Write-Output "Test log: $log"
