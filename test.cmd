@echo off
setlocal
cd /d "%~dp0"
if not exist build mkdir build
cl /nologo /std:c++17 /EHsc /utf-8 /W4 /DUNICODE /D_UNICODE /DNOMINMAX /MT tests\tests.cpp src\core.cpp /Fo:build\ /Fe:build\tests.exe
if errorlevel 1 exit /b 1
if "%~1"=="" (
  build\tests.exe
) else (
  build\tests.exe "%~1" "test-output\run-%RANDOM%-%RANDOM%"
)
exit /b %errorlevel%
