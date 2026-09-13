[CmdletBinding()]
param(
    [string]$Client = "$PSScriptRoot/../build-ap-reuse/Release/quake3e.x64.exe",
    [string]$GameRoot = "$PSScriptRoot/../smoke-ap"
)

$ErrorActionPreference = 'Stop'
$Client = (Resolve-Path -LiteralPath $Client).Path
$GameRoot = (Resolve-Path -LiteralPath $GameRoot).Path
$testRoot = Join-Path $PSScriptRoot "../build-ap-reuse/mode-test-$([guid]::NewGuid())"
New-Item -ItemType Directory -Path "$testRoot/cpma-ap" -Force | Out-Null
$testRoot = (Resolve-Path -LiteralPath $testRoot).Path
$stages = @('1v1 cpm15 1', 'ffa q3dm1 0', '1v1 cpm15 1', 'ffa q3dm1 0')
$commands = @('set server_useMapModes 0')
for ($i = 0; $i -lt $stages.Count; $i++) {
    $mode, $map, $gameType = $stages[$i].Split(' ')
    $commands += @("set mode_start $mode", "set g_gametype $gameType", 'set g_warmup 0',
        'set fraglimit 10', 'set timelimit 0', "map $map", 'ap_cpma_stage_limits',
        "echo AP_MODE_$i", 'mode_current', 'g_gametype')
}
$commands += @('addbot sarge 50', 'addbot visor 50', 'addbot major 50', 'wait 10', 'quit')
$commands | Set-Content -LiteralPath "$testRoot/cpma-ap/check.cfg"
$arguments = @('+set', 'dedicated', '1', '+set', 'fs_basepath', "`"$GameRoot`"",
    '+set', 'fs_homepath', "`"$testRoot`"", '+set', 'fs_basegame', 'baseq3/cpma',
    '+set', 'fs_game', 'cpma-ap', '+set', 'vm_game', '2', '+set', 'sv_pure', '0',
    '+set', 'net_ip', '127.0.0.1', '+set', 'net_port', '27965',
    '+set', 'logfile', '2', '+exec', 'check.cfg')
$process = Start-Process -FilePath $Client -ArgumentList $arguments -WindowStyle Hidden -PassThru
if (-not $process.WaitForExit(20000)) {
    Stop-Process -Id $process.Id
    throw "CPMA mode test timed out. Logs: $testRoot"
}
$log = Get-Content -LiteralPath "$testRoot/cpma-ap/qconsole.log" -Raw
if ($process.ExitCode -ne 0 -or $log -match 'could not apply CPMA stage limits|Invalid vote') {
    throw "CPMA stage setup failed. Logs: $testRoot"
}
for ($i = 0; $i -lt $stages.Count; $i++) {
    $mode, $map, $gameType = $stages[$i].Split(' ')
    if ($log -notmatch "AP_MODE_$i\r?\n`"mode_current`" is:`"$mode\^7`"\r?\n`"g_gametype`" is:`"$gameType\^7`"") {
        throw "Stage $i ($map) did not select $mode. Logs: $testRoot"
    }
}
foreach ($bot in 'Sarge', 'Visor', 'Major') {
    if ($log -notmatch "ClientUserinfoChanged: [0-2] n\\[^\r\n]*$bot\^7\\t\\0\\") {
        throw "FFA bot $bot did not join the playing team. Logs: $testRoot"
    }
}
Write-Output "PASS: duel/FFA/duel/FFA in one process, with three active FFA bots. Logs: $testRoot"
