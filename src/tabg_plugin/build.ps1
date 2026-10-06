# Build TABG Portal Passthrough plugin (Release)
$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ScriptDir
dotnet build -c Release @args
