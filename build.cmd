@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"
if not exist build mkdir build
set "taskVsWhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%taskVsWhere%" (
  echo Visual Studio Installer with the C++ x86 tools is required.
  exit /b 2
)
set "taskVsPath="
for /f "usebackq tokens=*" %%i in (`"%taskVsWhere%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "taskVsPath=%%i"
if not defined taskVsPath exit /b 2
call "%taskVsPath%\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b 1
if not exist vendor\lua-5.2.4\src\lua.h exit /b 10
if not exist build\lua52.lib (
  if not exist build\luaobj mkdir build\luaobj
  pushd build\luaobj
  set taskLuaSources=
  for %%f in (..\..\vendor\lua-5.2.4\src\*.c) do if /I not "%%~nf"=="lua" if /I not "%%~nf"=="luac" set taskLuaSources=!taskLuaSources! "%%f"
  cl /nologo /TC /MT /O2 /c /D_CRT_SECURE_NO_WARNINGS !taskLuaSources!
  if errorlevel 1 exit /b 1
  set taskLuaObjects=
  for %%f in (*.obj) do if /I not "%%~nf"=="lua" if /I not "%%~nf"=="luac" set taskLuaObjects=!taskLuaObjects! "%%f"
  lib /nologo /OUT:..\lua52.lib !taskLuaObjects!
  popd
)
cl /nologo /std:c++17 /W4 /EHsc /MT /O2 /Zi /DWIN32_LEAN_AND_MEAN /DNOMINMAX /Ivendor\lua-5.2.4\src /LD src\plugin.cpp src\snapshot.cpp src\runtime_state.cpp src\preview_window.cpp src\room_art.cpp src\asset_mesh.cpp src\particle_sim.cpp src\native_render.cpp src\native_scene.cpp /Fobuild\ /Febuild\recursed_peek.dll /link /DEF:src\plugin.def build\lua52.lib opengl32.lib user32.lib gdi32.lib gdiplus.lib shell32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /EHsc /MT /O2 /Zi /DWIN32_LEAN_AND_MEAN /DNOMINMAX src\launcher.cpp /Fobuild\launcher.obj /Febuild\recursed_peek.exe /link bcrypt.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /EHsc /MT /O2 /DWIN32_LEAN_AND_MEAN /DNOMINMAX tools\probe.cpp /Fobuild\probe.obj /Febuild\probe.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /EHsc /MT /O2 /DNOMINMAX /Ivendor\lua-5.2.4\src tests\snapshot_test.cpp src\snapshot.cpp src\runtime_state.cpp src\asset_mesh.cpp src\particle_sim.cpp /Fobuild\ /Febuild\snapshot_test.exe /link build\lua52.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /EHsc /MT /O2 /DNOMINMAX /Ivendor\lua-5.2.4\src tests\render_test.cpp src\snapshot.cpp src\room_art.cpp src\asset_mesh.cpp src\particle_sim.cpp /Fobuild\ /Febuild\render_test.exe /link build\lua52.lib gdiplus.lib gdi32.lib user32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /EHsc /MT /O2 /DNOMINMAX /Ivendor\lua-5.2.4\src tests\ci_test.cpp src\snapshot.cpp src\runtime_state.cpp src\particle_sim.cpp /Fobuild\ /Febuild\ci_test.exe /link build\lua52.lib
exit /b %errorlevel%
