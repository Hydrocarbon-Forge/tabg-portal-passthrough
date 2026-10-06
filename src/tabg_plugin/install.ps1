# install.ps1 - Install TABG Portal Passthrough plugin into game directory.
# Enforces safety check for EasyAntiCheat directory per TASK_BUILD slice D + G.2.
param (
    [string]$GameDir = "$PSScriptRoot/../../games/TotallyAccurateBattlegrounds"
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

if (!(Test-Path $GameDir)) {
    Write-Error "Target game directory not found: $GameDir"
}

# Check if EasyAntiCheat folder exists in the game directory
$eacPath = Join-Path $GameDir "EasyAntiCheat"
$eacDataPath = Join-Path $GameDir "TotallyAccurateBattlegrounds_Data/Plugins/x86_64/easyanticheat_x64.dll"
if ((Test-Path $eacPath) -or (Test-Path $eacDataPath)) {
    Write-Warning "====================================================================="
    Write-Warning "[WARNING] EasyAntiCheat presence detected in target game installation!"
    Write-Warning "Using client-side shims with an active EAC client against official servers"
    Write-Warning "will result in EAC flagging/kicks. Use ONLY with a dedicated local server."
    Write-Warning "====================================================================="
}

$pluginsDir = Join-Path $GameDir "BepInEx/plugins/TabgPortal"
if (!(Test-Path $pluginsDir)) {
    New-Item -ItemType Directory -Path $pluginsDir -Force | Out-Null
}

$binDir = Join-Path $ScriptDir "bin/Release"
if (!(Test-Path $binDir)) {
    $binDir = Join-Path $ScriptDir "bin/Debug"
}

if (!(Test-Path (Join-Path $binDir "TabgPortalPassthrough.dll"))) {
    Write-Error "Plugin binary not found in $binDir. Run build.ps1 first."
}

Copy-Item -Path (Join-Path $binDir "*") -Destination $pluginsDir -Recurse -Force
Write-Host "[OK] Installed TABG Portal Passthrough plugin to $pluginsDir"
