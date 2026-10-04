@echo off
setlocal
call "%VCVARS%" >nul 2>nul || exit /b 1
cd /d "%~dp0"
if not exist ..\build mkdir ..\build
cl /nologo /O2 /EHsc /std:c++20 /MT pause_overlay_test.cpp /Fe:..\build\pause_overlay_test.exe /Fo:..\build\pause_overlay_test.obj || exit /b 1
..\build\pause_overlay_test.exe
