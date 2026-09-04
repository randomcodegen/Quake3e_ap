[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $GameRoot,
    [string] $BundleRoot = (Join-Path $PSScriptRoot '..\dist\bundle'),
    [switch] $Restore
)

$ErrorActionPreference = 'Stop'
$game = [IO.Path]::GetFullPath($GameRoot)
$bundle = [IO.Path]::GetFullPath($BundleRoot)
$pak0 = Join-Path $game 'baseq3\pak0.pk3'
$marker = Join-Path $game '.q3ap-installed'
$isReinstall = Test-Path -LiteralPath $marker -PathType Leaf
$expectedPak0 = '7CE8B3910620CD50A09E4F1100F426E8C6180F68895D589F80E6BD95AF54BCAE'

$relativeFiles = @('quake3e.x64.exe', 'quake3e.ded.x64.exe', 'quake3e_opengl_x86_64.dll',
    'glib-2.0-0.dll', 'iconv-2.dll', 'intl-8.dll', 'jansson.dll', 'libcrypto-3-x64.dll',
    'libssl-3-x64.dll', 'pcre2-8.dll', 'uv.dll', 'websockets.dll', 'z.dll',
    'q3ap\qagamex86_64.dll', 'q3ap\uix86_64.dll',
    'q3ap\models\powerups\ap\filler.md3', 'q3ap\models\powerups\ap\useful.md3',
    'q3ap\models\powerups\ap\progression.md3', 'q3ap\models\powerups\ap\trap.md3',
    'q3ap\scripts\q3ap_markers.shader', 'q3ap-launch.cmd')

if ($Restore) {
    if (-not (Test-Path -LiteralPath $marker -PathType Leaf)) { throw "No q3ap installation marker found in $game" }
} else {
    if (-not (Test-Path -LiteralPath $pak0 -PathType Leaf)) { throw "baseq3\pak0.pk3 not found below $game" }
    if ((Get-FileHash -LiteralPath $pak0 -Algorithm SHA256).Hash -ne $expectedPak0) { throw 'Unsupported pak0.pk3 hash' }
    foreach ($relative in $relativeFiles | Where-Object { $_ -ne 'q3ap-launch.cmd' }) {
        $source = Join-Path $bundle $relative
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Bundle file missing: $source" }
    }
    Set-Content -LiteralPath $marker -Encoding Ascii -Value 'Quake III Archipelago installation'
}

foreach ($relative in $relativeFiles) {
    $target = [IO.Path]::GetFullPath((Join-Path $game $relative))
    if (-not $target.StartsWith($game, [StringComparison]::OrdinalIgnoreCase)) { throw "Unsafe install target: $target" }
    $backup = "$target.q3ap-backup"
    if ($Restore) {
        if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Force }
        if (Test-Path -LiteralPath $backup) { Move-Item -LiteralPath $backup -Destination $target }
        continue
    }
    if ($isReinstall) { continue }
    if (-not (Test-Path -LiteralPath $target) -or (Test-Path -LiteralPath $backup)) { continue }
    Move-Item -LiteralPath $target -Destination $backup
}

if ($Restore) {
    Remove-Item -LiteralPath $marker -Force
    Write-Host "Restored q3ap installation in $game"
    return
}
New-Item -ItemType Directory -Path (Join-Path $game 'q3ap') -Force | Out-Null
foreach ($relative in $relativeFiles | Where-Object { $_ -ne 'q3ap-launch.cmd' }) {
    $source = Join-Path $bundle $relative
    $destination = Join-Path $game $relative
    New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination -Force
}
Set-Content -LiteralPath (Join-Path $game 'q3ap-launch.cmd') -Encoding Ascii -Value '@echo off', 'start "Quake III Archipelago" "%~dp0quake3e.x64.exe" +set fs_game q3ap +set vm_game 0 +set vm_ui 0'
Write-Host "Installed q3ap in $game. Run q3ap-launch.cmd."
