[CmdletBinding()]
param(
    [ValidateSet('vulkan', 'opengl')]
    [string]$Renderer = 'vulkan'
)
& (Join-Path $PSScriptRoot 'launch-client.ps1') -CPMA -Renderer $Renderer
