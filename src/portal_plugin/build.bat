@echo off
rem build.bat — build passthrough.dll, the 32-bit Source server plugin, with MSVC.
rem Run on the WINDOWS box. Portal is 32-bit (recon/portal-notes.md §1) so this
rem MUST use the x86 toolchain. Set SDK to your source-sdk-2013 checkout.
rem
rem   set SDK=C:\dev\source-sdk-2013\sp\src
rem   build.bat
rem
rem The real SDK public headers replace sdk_stubs\ at build time: we add the SDK
rem include dirs FIRST so <eiface.h>, <cdll_int.h>, <materialsystem/imaterialsystem.h>
rem resolve to Valve's headers, and keep sdk_stubs\ off the Windows include path.
setlocal
set HERE=%~dp0
set FC=%HERE%..\framechannel\cpp

if not defined VCVARS for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VCVARS=%%i\VC\Auxiliary\Build\vcvars32.bat"
call "%VCVARS%" >nul || (echo could not init MSVC x86 env & exit /b 1)

if not defined SDK (echo Set SDK to your source-sdk-2013\sp\src checkout & exit /b 1)
if not exist "%HERE%build" mkdir "%HERE%build"

rem Include order: SDK public headers win over sdk_stubs (which is NOT referenced
rem on Windows). framechannel.h is shared with the host/tests, included as-is.
cl /nologo /LD /O2 /EHsc- /GR- /MT /W3 /DWIN32 /D_WINDOWS /DWIN32_LEAN_AND_MEAN /DNOMINMAX ^
  /I "%SDK%\public" /I "%SDK%\public\tier0" /I "%SDK%\public\tier1" ^
  /I "%SDK%\common" /I "%FC%" /I "%HERE%" ^
  "%HERE%passthrough.cpp" "%HERE%ws.cpp" ^
  /Fo"%HERE%build\\" /Fe"%HERE%build\passthrough.dll" ^
  /link /MACHINE:X86 ws2_32.lib user32.lib ^
  "%SDK%\lib\public\tier0.lib" "%SDK%\lib\public\tier1.lib"

if errorlevel 1 (echo BUILD FAILED & exit /b 1)
echo built build\passthrough.dll (x86)
