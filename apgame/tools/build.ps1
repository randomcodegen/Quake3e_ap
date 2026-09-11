[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $VcpkgRoot,
    [string] $CMake = 'cmake',
    [string] $Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$moduleRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$engineRoot = (Resolve-Path (Join-Path $moduleRoot '..')).Path
$apccRoot = (Resolve-Path (Join-Path $engineRoot 'APCc')).Path
$toolchain = Join-Path $VcpkgRoot 'scripts\buildsystems\vcpkg.cmake'
if (-not (Test-Path -LiteralPath $toolchain -PathType Leaf)) { throw "vcpkg toolchain not found: $toolchain" }
if (-not (Get-Command $CMake -ErrorAction SilentlyContinue)) { throw "CMake executable not found: $CMake" }

$engineBuild = Join-Path $engineRoot 'build-bundle'
$moduleBuild = Join-Path $moduleRoot 'build-bundle'
$bundle = Join-Path $moduleRoot 'dist\bundle'

& $CMake -S $engineRoot -B $engineBuild "-DCMAKE_TOOLCHAIN_FILE=$toolchain" "-DAPCC_ROOT=$apccRoot" '-DVCPKG_TARGET_TRIPLET=x64-windows'
if ($LASTEXITCODE) { throw 'Engine configure failed' }
& $CMake --build $engineBuild --config $Configuration --target 'quake3e.x64' 'quake3e.ded.x64' 'quake3e_opengl_x86_64' q3ap_abi_test q3ap_state_test q3ap_runtime_state_test
if ($LASTEXITCODE) { throw 'Engine build failed' }
& $CMake --build $engineBuild --config $Configuration --target RUN_TESTS
if ($LASTEXITCODE) { throw 'Engine tests failed' }
& $CMake -S $moduleRoot -B $moduleBuild
if ($LASTEXITCODE) { throw 'Module configure failed' }
& $CMake --build $moduleBuild --config $Configuration --target qagame ui q3ap_game_state_test
if ($LASTEXITCODE) { throw 'Module build failed' }
& $CMake --build $moduleBuild --config $Configuration --target RUN_TESTS
if ($LASTEXITCODE) { throw 'Module tests failed' }

$resolvedBundle = [IO.Path]::GetFullPath($bundle)
$resolvedDist = [IO.Path]::GetFullPath((Join-Path $moduleRoot 'dist'))
if (-not $resolvedBundle.StartsWith($resolvedDist, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe bundle path' }
if (Test-Path -LiteralPath $resolvedBundle) { Remove-Item -LiteralPath $resolvedBundle -Recurse -Force }
New-Item -ItemType Directory -Path (Join-Path $resolvedBundle 'q3ap') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $resolvedBundle 'licenses') -Force | Out-Null
Copy-Item -Path (Join-Path $moduleRoot 'assets\*') -Destination (Join-Path $resolvedBundle 'q3ap') -Recurse -Force

$engineOutput = Join-Path $engineBuild $Configuration
$moduleOutput = Join-Path $moduleBuild $Configuration
$files = @('quake3e.x64.exe', 'quake3e.ded.x64.exe', 'quake3e_opengl_x86_64.dll',
    'glib-2.0-0.dll', 'iconv-2.dll', 'intl-8.dll', 'jansson.dll', 'libcrypto-3-x64.dll',
    'libssl-3-x64.dll', 'pcre2-8.dll', 'uv.dll', 'websockets.dll', 'z.dll')
foreach ($name in $files) {
    $source = Join-Path $engineOutput $name
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Required bundle file missing: $source" }
    Copy-Item -LiteralPath $source -Destination (Join-Path $resolvedBundle $name)
}
Copy-Item -LiteralPath (Join-Path $moduleOutput 'qagamex86_64.dll') -Destination (Join-Path $resolvedBundle 'q3ap\qagamex86_64.dll')
Copy-Item -LiteralPath (Join-Path $moduleOutput 'uix86_64.dll') -Destination (Join-Path $resolvedBundle 'q3ap\uix86_64.dll')
Copy-Item -LiteralPath (Join-Path $engineRoot 'COPYING.txt') -Destination (Join-Path $resolvedBundle 'licenses\Quake3e-GPL-2.0.txt')
Copy-Item -LiteralPath (Join-Path $moduleRoot 'UPSTREAM.md') -Destination (Join-Path $resolvedBundle 'licenses\q3ap-upstream.md')
Copy-Item -LiteralPath (Join-Path $apccRoot 'README.md') -Destination (Join-Path $resolvedBundle 'licenses\APCc-README.md')

$installed = Join-Path $engineBuild 'vcpkg_installed\x64-windows\share'
foreach ($package in @('glib', 'jansson', 'libwebsockets', 'openssl', 'pcre2', 'libuv', 'zlib', 'libiconv', 'gettext')) {
    $copyright = Join-Path $installed "$package\copyright"
    if (Test-Path -LiteralPath $copyright) { Copy-Item -LiteralPath $copyright -Destination (Join-Path $resolvedBundle "licenses\$package.txt") }
}
Write-Host "Bundle staged at $resolvedBundle"
