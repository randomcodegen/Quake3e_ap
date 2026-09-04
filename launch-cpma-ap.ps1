[CmdletBinding()]
param(
    [ValidateSet('opengl', 'vulkan')]
    [string]$Renderer = 'vulkan'
)

$ErrorActionPreference = 'Stop'
$gameRoot = Join-Path $PSScriptRoot 'smoke-ap'
$client = Join-Path $gameRoot 'quake3e.x64.exe'
$prototype = Join-Path $gameRoot 'cpma-ap'
$cpmaPak = Join-Path $gameRoot 'cpma\z-cpma-pak153.pk3'
$apUi = Join-Path $gameRoot 'q3ap\uix86_64.dll'
$apModels = Join-Path $gameRoot 'q3ap\models\powerups\ap'
$apShader = Join-Path $gameRoot 'q3ap\scripts\q3ap_markers.shader'

if (-not (Test-Path -LiteralPath $client -PathType Leaf)) { throw "Client not found: $client" }
if (-not (Test-Path -LiteralPath (Join-Path $gameRoot 'baseq3\pak0.pk3') -PathType Leaf)) { throw "Quake III pak0.pk3 not found below $gameRoot" }
if (-not (Test-Path -LiteralPath $cpmaPak -PathType Leaf)) { throw "CPMA 1.53 not found: $cpmaPak" }
if (-not (Test-Path -LiteralPath $apUi -PathType Leaf)) { throw "Archipelago UI not found: $apUi" }
if (-not (Test-Path -LiteralPath $apShader -PathType Leaf)) { throw "Archipelago marker shader not found: $apShader" }

New-Item -ItemType Directory -Force -Path $prototype | Out-Null
Copy-Item -LiteralPath $apUi -Destination (Join-Path $prototype 'uix86_64.dll') -Force
$markerModels = Join-Path $prototype 'models\powerups\ap'
$markerScripts = Join-Path $prototype 'scripts'
New-Item -ItemType Directory -Force -Path $markerModels, $markerScripts | Out-Null
foreach ($model in 'filler.md3', 'useful.md3', 'progression.md3', 'trap.md3') {
    Copy-Item -LiteralPath (Join-Path $apModels $model) -Destination (Join-Path $markerModels $model) -Force
}
Copy-Item -LiteralPath $apShader -Destination (Join-Path $markerScripts 'q3ap_markers.shader') -Force

$arguments = @(
    '+set', 'cl_renderer', $Renderer,
    '+set', 'r_fullscreen', '0',
    '+set', 'fs_basegame', 'baseq3/cpma',
    '+set', 'fs_game', 'cpma-ap',
    '+set', 'sv_pure', '0',
	'+set', 'sv_cheats', '1',
	'+set', 'cg_predictItems', '0',
	'+set', 'vm_game', '2',
	'+set', 'vm_cgame', '2',
	'+set', 'vm_ui', '0',
	'+set', 'logfile', '2',
    '+setu', 'osp_client', '20231024'
)
Start-Process -FilePath $client -WorkingDirectory $gameRoot -ArgumentList $arguments
