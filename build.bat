@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul

echo [1/3] Running TDD unit tests...
cl /nologo /EHsc /std:c++17 tests\parser_test.cpp /Fe:tests\parser_test.exe || exit /b 1
tests\parser_test.exe || exit /b 1

echo [2/3] Compiling Win32 Icon Resource (ghost.rc)...
rc /nologo /fo src\ghost.res src\ghost.rc || exit /b 1

echo [3/3] Building portable GhostMirror.exe...
if not exist dist\GhostMirror mkdir dist\GhostMirror
cl /nologo /O2 /EHsc /std:c++17 src\main.cpp src\ghost.res /Fe:dist\GhostMirror\GhostMirror.exe /link /SUBSYSTEM:WINDOWS || exit /b 1
echo Build complete: dist\GhostMirror\GhostMirror.exe
