[CmdletBinding()]
param(
    [ValidateSet('opengl', 'vulkan')]
    [string]$Renderer = 'vulkan'
)

$ErrorActionPreference = 'Stop'
$gameRoot = Join-Path $PSScriptRoot 'smoke-ap'
$client = Join-Path $gameRoot 'quake3e.x64.exe'
if (-not (Test-Path -LiteralPath $client -PathType Leaf)) { throw "Client not found: $client" }
if (-not (Test-Path -LiteralPath (Join-Path $gameRoot 'baseq3\pak0.pk3') -PathType Leaf)) { throw "Quake III pak0.pk3 not found below $gameRoot" }

$arguments = @(
    '+set', 'cl_renderer', $Renderer,
    '+set', 'fs_game', 'q3ap',
    '+set', 'vm_game', '0',
    '+set', 'vm_ui', '0',
    '+set', 'logfile', '2'
)
Start-Process -FilePath $client -WorkingDirectory $gameRoot -ArgumentList $arguments
