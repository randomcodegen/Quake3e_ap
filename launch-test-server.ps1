[CmdletBinding()]
param([int]$Port = 38282)

$ErrorActionPreference = 'Stop'
$serverRoot = 'C:\Users\Rando\Documents\projects\Archipelago_q3'
$python = 'C:\Users\Rando\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
$seedRoot = Get-Content -LiteralPath (Join-Path $serverRoot '.codex-last-seed-path.txt')
$seed = Get-ChildItem -LiteralPath $seedRoot -Filter '*.zip' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $seed) { throw "No seed archive found in $seedRoot" }
if (Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue) {
    Write-Host "Server already listening on 127.0.0.1:$Port"
    exit 0
}

$env:PYTHONPATH = Join-Path $serverRoot '.python_deps'
$env:SKIP_REQUIREMENTS_UPDATE = '1'
$startInfo = [Diagnostics.ProcessStartInfo]::new()
$startInfo.FileName = $python
$startInfo.Arguments = "MultiServer.py `"$($seed.FullName)`" --host 127.0.0.1 --port $Port --logtime"
$startInfo.WorkingDirectory = $serverRoot
$startInfo.UseShellExecute = $true
$startInfo.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
[void][Diagnostics.Process]::Start($startInfo)

for ($attempt = 0; $attempt -lt 20; $attempt++) {
    Start-Sleep -Milliseconds 250
    if (Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue) {
        Write-Host "Server listening on 127.0.0.1:$Port"
        exit 0
    }
}
throw "Server failed to listen on port $Port"
