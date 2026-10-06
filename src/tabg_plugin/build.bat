@echo off
rem Build TABG Portal Passthrough plugin (Release)
setlocal
cd /d "%~dp0"
dotnet build -c Release %*
endlocal
