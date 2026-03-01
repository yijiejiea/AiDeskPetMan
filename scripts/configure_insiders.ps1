param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",
    [string]$DepsRoot = "",
    [string]$CubismSdkPath = "",
    [ValidateSet("Auto", "On", "Off")]
    [string]$Live2D = "Auto",
    [switch]$EnableLive2D,
    [ValidateSet("Auto", "On", "Off")]
    [string]$Llama = "Auto",
    [switch]$EnableLlama,
    [switch]$UseVcpkg
)

$ErrorActionPreference = "Stop"

$script_root = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $script_root "local_paths.ps1")

function Resolve-Live2dEnabled {
    param(
        [string]$Live2DMode,
        [bool]$ForceEnable,
        [string]$ScriptRoot
    )

    if ($ForceEnable -or $Live2DMode -eq "On") {
        return $true
    }
    if ($Live2DMode -eq "Off") {
        return $false
    }

    $config_path = Join-Path (Join-Path $ScriptRoot "..") "config\config.json"
    $config_path = [System.IO.Path]::GetFullPath($config_path)
    if (!(Test-Path $config_path)) {
        Write-Host "Live2D auto mode: config file not found, fallback OFF. path: $config_path"
        return $false
    }

    try {
        $config_text = Get-Content -Path $config_path -Raw -Encoding UTF8
        $config_json = $config_text | ConvertFrom-Json
        if ($null -ne $config_json.skin -and $null -ne $config_json.skin.enable_live2d) {
            $enabled = [bool]$config_json.skin.enable_live2d
            Write-Host "Live2D auto mode: config.skin.enable_live2d=$enabled"
            return $enabled
        }

        Write-Host "Live2D auto mode: skin.enable_live2d missing, fallback OFF."
        return $false
    } catch {
        Write-Warning "Live2D auto mode: failed to parse $config_path, fallback OFF. error: $($_.Exception.Message)"
        return $false
    }
}

function Resolve-LlamaEnabled {
    param(
        [string]$LlamaMode,
        [bool]$ForceEnable,
        [string]$ScriptRoot
    )

    if ($ForceEnable -or $LlamaMode -eq "On") {
        return $true
    }
    if ($LlamaMode -eq "Off") {
        return $false
    }

    $config_path = Join-Path (Join-Path $ScriptRoot "..") "config\config.json"
    $config_path = [System.IO.Path]::GetFullPath($config_path)
    if (!(Test-Path $config_path)) {
        Write-Host "Llama auto mode: config file not found, fallback OFF. path: $config_path"
        return $false
    }

    try {
        $config_text = Get-Content -Path $config_path -Raw -Encoding UTF8
        $config_json = $config_text | ConvertFrom-Json
        if ($null -ne $config_json.ai -and $null -ne $config_json.ai.inference_mode) {
            $enabled = [string]$config_json.ai.inference_mode -eq "local_model"
            Write-Host "Llama auto mode: config.ai.inference_mode=$($config_json.ai.inference_mode), enabled=$enabled"
            return $enabled
        }

        Write-Host "Llama auto mode: ai.inference_mode missing, fallback OFF."
        return $false
    } catch {
        Write-Warning "Llama auto mode: failed to parse $config_path, fallback OFF. error: $($_.Exception.Message)"
        return $false
    }
}

$vcvars_path = $script:VcvarsPath
if (!(Test-Path $vcvars_path)) {
    throw "vcvars64.bat not found: $vcvars_path"
}

if (-not $DepsRoot) {
    $DepsRoot = $script:DefaultDepsRoot
}

$live2d_enabled = Resolve-Live2dEnabled -Live2DMode $Live2D -ForceEnable:$EnableLive2D -ScriptRoot $script_root
$live2d_value = if ($live2d_enabled) { "ON" } else { "OFF" }
$llama_enabled = Resolve-LlamaEnabled -LlamaMode $Llama -ForceEnable:$EnableLlama -ScriptRoot $script_root
$llama_value = if ($llama_enabled) { "ON" } else { "OFF" }

if (-not $CubismSdkPath) {
    $CubismSdkPath = $script:DefaultCubismSdkRoot
}

$cubism_exists = Test-Path $CubismSdkPath
if ($live2d_enabled -and -not $cubism_exists) {
    throw "Cubism SDK path does not exist: $CubismSdkPath"
}

$qt_root = $script:DefaultQtRoot
if (!(Test-Path $qt_root)) {
    throw "Qt root does not exist: $qt_root"
}

if ($UseVcpkg -and -not $env:VCPKG_ROOT) {
    $env:VCPKG_ROOT = "D:\visualstudioInsiders\VC\vcpkg"
}

if ($UseVcpkg -and !(Test-Path "$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake")) {
    throw "VCPKG_ROOT is invalid: $env:VCPKG_ROOT"
}

$extraArgs = @()
if ($UseVcpkg) {
    $extraArgs += "-DCMAKE_TOOLCHAIN_FILE=`"$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake`""
}
$extraArgs += "-DMIKUDESK_ENABLE_LIVE2D=$live2d_value"
$extraArgs += "-DMIKUDESK_ENABLE_LLAMA=$llama_value"
if ($cubism_exists) {
    $extraArgs += "-DMIKUDESK_CUBISM_SDK_PATH=`"$CubismSdkPath`""
}

$prefix_paths = @($qt_root)
if ($DepsRoot) {
    if (!(Test-Path $DepsRoot)) {
        throw "DepsRoot does not exist: $DepsRoot"
    }
    $prefix_paths += $DepsRoot
}
if ($prefix_paths.Count -gt 0) {
    $joined_prefix_paths = $prefix_paths -join ";"
    $extraArgs += "-DCMAKE_PREFIX_PATH=`"$joined_prefix_paths`""
}

$preset = if ($Config -eq "Debug") { $script:DebugPreset } else { $script:ReleasePreset }
$extraText = if ($extraArgs.Count -gt 0) { " " + ($extraArgs -join " ") } else { "" }
$command = "call `"$vcvars_path`" && cmake --fresh --preset $preset$extraText"

${stdout_file} = [System.IO.Path]::GetTempFileName()
${stderr_file} = [System.IO.Path]::GetTempFileName()
try {
    $process = Start-Process -FilePath "cmd.exe" `
        -ArgumentList "/c", $command `
        -NoNewWindow `
        -Wait `
        -PassThru `
        -RedirectStandardOutput $stdout_file `
        -RedirectStandardError $stderr_file

    if (Test-Path $stdout_file) {
        Get-Content $stdout_file | ForEach-Object { Write-Host $_ }
    }
    if (Test-Path $stderr_file) {
        Get-Content $stderr_file | ForEach-Object { Write-Host $_ }
    }

    if ($process.ExitCode -ne 0) {
        throw "Configure failed with exit code $($process.ExitCode)"
    }
}
finally {
    Remove-Item $stdout_file, $stderr_file -ErrorAction SilentlyContinue
}

Write-Host "Configure succeeded with preset: $preset"
