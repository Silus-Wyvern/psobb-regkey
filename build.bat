@echo off
rem ==========================================================================
rem  psobb-regkey - command-line build (no solution needed).
rem
rem  Needs Visual Studio with "Desktop development with C++" (the 32-bit
rem  MSVC tools). Finds vcvars32.bat with vswhere; if that fails, falls back
rem  to the Visual Studio 18 Community path. If yours differs, set VCVARS
rem  yourself before running, e.g.
rem     set "VCVARS=C:\...\VC\Auxiliary\Build\vcvars32.bat"
rem
rem  Output: build\psobb_regkey.asi
rem ==========================================================================
setlocal
cd /d "%~dp0"

if defined VCVARS goto :have_vcvars

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :fallback
set "VSPATH="
for /f "usebackq tokens=*" %%I in (`call "%VSWHERE%" -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%I"
if not defined VSPATH goto :fallback
set "VCVARS=%VSPATH%\VC\Auxiliary\Build\vcvars32.bat"
if exist "%VCVARS%" goto :have_vcvars

:fallback
set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat"

:have_vcvars
if not exist "%VCVARS%" goto :no_vcvars
call "%VCVARS%" >nul

if not exist build mkdir build
del /q build\*.obj build\psobb_regkey.* 2>nul

cl /nologo /LD /O2 /W3 /EHsc /std:c++20 /MT /permissive- ^
   /DWIN32 /DNDEBUG /D_WINDOWS /D_USRDLL /DUNICODE /D_UNICODE ^
   /Fobuild\ /Fdbuild\ ^
   src\config.cpp src\dllmain.cpp src\log.cpp src\migrate.cpp src\verify.cpp ^
   user32.lib advapi32.lib ^
   /link /DLL /MACHINE:X86 /SUBSYSTEM:WINDOWS /OUT:build\psobb_regkey.asi
if errorlevel 1 exit /b 1

echo.
echo ----- build\psobb_regkey.asi built -----
dir /b build\psobb_regkey.asi
endlocal
exit /b 0

:no_vcvars
echo Could not find vcvars32.bat at:
echo   %VCVARS%
echo Set VCVARS to your Visual Studio's vcvars32.bat and run again.
exit /b 1
