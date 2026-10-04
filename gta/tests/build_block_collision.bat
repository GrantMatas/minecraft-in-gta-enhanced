@echo off
setlocal
call "%VCVARS%" >nul 2>nul || exit /b 1
cd /d "%~dp0"
cl /nologo /O2 /EHsc /std:c++20 /MT block_collision_test.cpp /Fe:..\build\block_collision_test.exe /Fo:..\build\block_collision_test.obj || exit /b 1
..\build\block_collision_test.exe
