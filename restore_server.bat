@echo off
REM restore_server.bat - restore the TABG dedicated-server Assembly-CSharp.dll
REM from the pristine backup created by the patcher (G.1).
REM
REM Run from the project root on the Windows box (or wherever the server lives;
REM edit DLL below if your server is installed elsewhere).

setlocal
set "DLL=games\TotallyAccurateBattlegroundsDedicatedServer\TABG_Data\Managed\Assembly-CSharp.dll"
set "ORIG=%DLL%.orig"

if not exist "%ORIG%" (
    echo ERROR: backup not found: "%ORIG%"
    echo Nothing to restore. ^(Was the server ever patched on this machine?^)
    exit /b 1
)

copy /y "%ORIG%" "%DLL%" >nul
if errorlevel 1 (
    echo ERROR: failed to copy "%ORIG%" over "%DLL%".
    exit /b 1
)

echo Restored original -> "%DLL%"
echo ^(The .orig backup is left in place.^)
endlocal
