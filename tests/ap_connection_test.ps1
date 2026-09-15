[CmdletBinding()]
param(
    [int]$Port = 38319,
    [string]$Slot = 'FragTest',
    [string]$Map = 'cpm15',
    [string]$Client = "$PSScriptRoot/../build-ap-reuse/Release/quake3e.x64.exe",
    [string]$GameRoot = "$PSScriptRoot/../smoke-ap"
)

# Run against a local AP server with a CPMA seed, the requested stage unlocked,
# and Connected slot data exceeding the default 1024-byte inflate buffer.
$ErrorActionPreference = 'Stop'
if ($Port -lt 1 -or $Port -gt 65535 -or $Slot -notmatch '^[\w-]+$' -or $Map -notmatch '^[\w-]+$') {
    throw 'Invalid port, slot, or map'
}
$Client = (Resolve-Path -LiteralPath $Client).Path
$GameRoot = (Resolve-Path -LiteralPath $GameRoot).Path
$testRoot = [IO.Path]::GetFullPath("$PSScriptRoot/../build-ap-reuse/connection-test-$([guid]::NewGuid())")
New-Item -ItemType Directory -Path "$testRoot/cpma-ap" -Force | Out-Null
@"
set ap_debug_timing 1
ap_connect 127.0.0.1 $Port $Slot
wait 200
ap_status
ap_start_stage $Map
wait 20
ap_disconnect
quit
"@ | Set-Content -LiteralPath "$testRoot/cpma-ap/check.cfg"
$arguments = @('+set', 'dedicated', '1', '+set', 'fs_basepath', "`"$GameRoot`"",
    '+set', 'fs_homepath', "`"$testRoot`"", '+set', 'fs_basegame', 'baseq3/cpma',
    '+set', 'fs_game', 'cpma-ap', '+set', 'vm_game', '2', '+set', 'sv_pure', '0',
    '+set', 'net_ip', '127.0.0.1', '+set', 'net_port', '27969',
    '+set', 'logfile', '2', '+map', $Map, '+exec', 'check.cfg')
$process = Start-Process -FilePath $Client -ArgumentList $arguments -WorkingDirectory $testRoot -WindowStyle Hidden -PassThru `
    -RedirectStandardOutput "$testRoot/client.stdout.log" -RedirectStandardError "$testRoot/client.stderr.log"
try {
    if (-not $process.WaitForExit(30000)) { throw "Connection test timed out. Logs: $testRoot" }
    $log = Get-Content -LiteralPath "$testRoot/cpma-ap/qconsole.log" -Raw
    $errors = Get-Content -LiteralPath "$testRoot/client.stderr.log" -Raw
    $connected = Get-Content -LiteralPath "$testRoot/client.stdout.log" |
        Where-Object { $_ -match '^in: \[.*"cmd"\s*:\s*"Connected"' } | Select-Object -First 1
    if ($process.ExitCode -ne 0 -or $errors -match 'JSON parse error' -or
        $errors -notmatch 'instantiating client ext permessage-deflate' -or
        $connected.Length -le 1024 -or
        $log -notmatch 'Archipelago: authenticated, slot data valid' -or
        $log -notmatch "Archipelago: starting .*\($Map\)" -or
        $log -notmatch 'CPMA stage limits applied') {
        throw "CPMA connection or stage startup failed. Logs: $testRoot"
    }
    Write-Output "PASS: WebSocket compression negotiated; CPMA authenticated, synchronized locations, and started $Map. Logs: $testRoot"
} finally {
    if (-not $process.HasExited) { Stop-Process -Id $process.Id }
}
