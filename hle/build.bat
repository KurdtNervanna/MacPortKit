@echo off
rem Development build of the HLE runtime (MSVC x64, console subsystem so the logs show).
rem Needs Visual Studio 2019/2022 or the Build Tools with "Desktop development with C++".
rem
rem   build.bat                         -> build\hle.exe with the defaults in src\config.h
rem   set HLE_DEFS=/DHLE_APP_NAME=\"My Game\" /DHLE_APP_FILE=\"MyGame.rsrc\"
rem   build.bat                         -> same, with your overrides
rem
rem IMPORTANT (learned the hard way): run this from PowerShell or cmd, e.g.
rem   powershell:  cmd /c ".\build.bat"
rem Git-Bash "cmd.exe /c build.bat" can silently do nothing (vcvars spawns a shell),
rem leaving a STALE exe that makes every later test result a lie.
rem Also kill a running hle.exe first, or the link step fails.
setlocal
cd /d "%~dp0"
set VCVARS=
set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist %VSWHERE% (
  for /f "usebackq tokens=*" %%i in (`%VSWHERE% -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
)
if not defined VCVARS set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (echo vcvars64.bat not found - install Visual Studio Build Tools with the C++ workload & exit /b 1)
call "%VCVARS%" >nul
set "V=dev"
if exist VERSION set /p V=<VERSION
if not exist build mkdir build
rem Musashi's opcode tables (m68kops.c/.h) are generated from m68k_in.c by m68kmake.
if not exist musashi\m68kops.c (
  cl /nologo /D_CRT_SECURE_NO_WARNINGS /Fo:build\ /Fe:build\m68kmake.exe musashi\m68kmake.c >nul || (echo m68kmake build failed & exit /b 1)
  build\m68kmake.exe musashi musashi\m68k_in.c >nul || (echo m68kmake failed & exit /b 1)
)
cl /nologo /O2 /Zi /W3 /wd4244 /wd4267 /wd4018 /wd4146 /D_CRT_SECURE_NO_WARNINGS /DHLE_VERSION=\"%V%\" %HLE_DEFS% /I musashi /Fo:build\ /Fe:build\hle.exe ^
   src\main.c src\trap.c src\mem.c src\rsrc.c src\qd.c src\rgn.c src\font.c src\wm.c src\menu.c src\ctl.c src\dlg.c src\evt.c src\fs.c src\pack.c src\plat.c src\host_win.c src\audio_win.c ^
   musashi\m68kcpu.c musashi\m68kops.c musashi\m68kdasm.c musashi\softfloat\softfloat.c ^
   /link user32.lib gdi32.lib winmm.lib comdlg32.lib shell32.lib ole32.lib /SUBSYSTEM:CONSOLE /DEBUG /OPT:REF
if errorlevel 1 (echo BUILD FAILED & exit /b 1)
echo BUILD OK:  build\hle.exe  (v%V%)
