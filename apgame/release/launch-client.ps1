[CmdletBinding()]
param(
    [ValidateSet('vulkan', 'opengl')]
    [string]$Renderer = 'vulkan',
    [switch]$CPMA
)

$ErrorActionPreference = 'Stop'
$client = Join-Path $PSScriptRoot 'quake3e.x64.exe'
foreach ($file in @('quake3e.x64.exe', 'baseq3\pak0.pk3', 'q3ap\uix86_64.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $PSScriptRoot $file) -PathType Leaf)) {
        throw "Required file missing: $file. See docs\setup_en.md."
    }
}
$arguments = @('+set', 'cl_renderer', $Renderer, '+set', 'r_fullscreen', '0',
    '+set', 'vm_ui', '0')
if ($CPMA) {
    if (-not (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'cpma\z-cpma-pak153.pk3'))) {
        throw 'CPMA 1.53 is required. See docs\setup_en.md.'
    }
    $arguments += @('+set', 'fs_basegame', 'baseq3/cpma', '+set', 'fs_game', 'cpma-ap',
        '+set', 'sv_pure', '0', '+set', 'cg_predictItems', '0',
        '+set', 'vm_game', '2', '+set', 'vm_cgame', '2', '+setu', 'osp_client', '20231024')
} else {
    $arguments += @('+set', 'fs_basegame', 'baseq3', '+set', 'fs_game', 'q3ap',
        '+set', 'vm_game', '0', '+set', 'vm_cgame', '2')
}
Start-Process -FilePath $client -WorkingDirectory $PSScriptRoot -ArgumentList $arguments
