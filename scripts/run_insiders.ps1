param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",
    [string]$CubismSdkPath = "",
    [ValidateSet("Auto", "On", "Off")]
    [string]$Live2D = "Auto",
    [switch]$EnableLive2D,
    [switch]$UseVcpkg,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$AppArgs
)

$ErrorActionPreference = "Stop"

$script_root = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $script_root "local_paths.ps1")

Get-Process -Name "mikudesk" -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

& (Join-Path $script_root "build_insiders.ps1") `
    -Config $Config `
    -Live2D $Live2D `
    -EnableLive2D:$EnableLive2D `
    -CubismSdkPath $CubismSdkPath `
    -UseVcpkg:$UseVcpkg
if ($LASTEXITCODE -ne 0) {
    throw "Build script failed with exit code $LASTEXITCODE"
}

$deps_root = $script:DefaultDepsRoot
if ($UseVcpkg -and (Test-Path "$env:VCPKG_ROOT\installed\x64-windows")) {
    $deps_root = "$env:VCPKG_ROOT\installed\x64-windows"
}

$qt_root = $script:DefaultQtRoot
if (!(Test-Path $qt_root)) {
    throw "Qt root does not exist: $qt_root"
}

$path_items = @()
if (Test-Path "$qt_root\bin") {
    $path_items += "$qt_root\bin"
}
if (Test-Path "$deps_root\bin") {
    $path_items += "$deps_root\bin"
}
if ($Config -eq "Debug" -and (Test-Path "$deps_root\debug\bin")) {
    $path_items += "$deps_root\debug\bin"
}
if ($path_items.Count -gt 0) {
    $env:PATH = ($path_items -join ";") + ";" + $env:PATH
}
$env:QT_PLUGIN_PATH = "$qt_root\plugins"

$preset = if ($Config -eq "Debug") { $script:DebugPreset } else { $script:ReleasePreset }
$exe_path = Join-Path (Join-Path $PSScriptRoot "..\build\$preset\$Config") "mikudesk.exe"
$exe_path = [System.IO.Path]::GetFullPath($exe_path)

if (!(Test-Path $exe_path)) {
    throw "Executable not found: $exe_path"
}

Write-Host "Running: $exe_path"
& $exe_path @AppArgs
exit $LASTEXITCODE
