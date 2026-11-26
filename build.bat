@echo off
setlocal
set SRC=src\main.c src\fifo_queue.c src\app_logic.c src\settings.c src\clipboard.c
set TESTSRC=tests\test_main.c src\fifo_queue.c src\app_logic.c src\settings.c src\clipboard.c
if not defined CC set CC=clang
set DEFS=-D_WIN32_WINNT=0x0A00 -DNTDDI_VERSION=0x0A000004
set CFLAGS=-std=c11 -Wall -Wextra -Werror -O2 %DEFS%
set CLFLAGS=/nologo /W4 /WX /utf-8 /std:c11 /Isrc /D_WIN32_WINNT=0x0A00 /DNTDDI_VERSION=0x0A000004

if not exist build mkdir build

if "%~1"=="tests" (
    %CC% %CFLAGS% %TESTSRC% -Isrc -o build\fifo_tests.exe -luser32 -lkernel32 -ladvapi32 %2
    if errorlevel 1 exit /b 1
    goto :run_tests
)
if "%~1"=="msvc" (
    cl %CLFLAGS% /Fobuild\ /Fdbuild\ %SRC% /link user32.lib gdi32.lib shell32.lib advapi32.lib /SUBSYSTEM:WINDOWS /OUT:build\fifo_clipboard.exe
    if errorlevel 1 exit /b 1
    exit /b 0
)
if "%~1"=="msvc-tests" (
    cl %CLFLAGS% /Fobuild\ /Fdbuild\ %TESTSRC% /link user32.lib advapi32.lib /OUT:build\fifo_tests.exe
    if errorlevel 1 exit /b 1
    goto :run_tests
)
if "%~1"=="asan" (
    %CC% -std=c11 -Wall -Wextra -g -O0 %DEFS% %TESTSRC% -Isrc -fsanitize=address,undefined -o build\fifo_tests_asan.exe -luser32 -lkernel32 -ladvapi32
    if errorlevel 1 exit /b 1
    goto :run_asan
)
%CC% %CFLAGS% %SRC% -Isrc -o build\fifo_clipboard.exe -mwindows -luser32 -lgdi32 -lshell32 -ladvapi32
if errorlevel 1 exit /b 1
exit /b 0

:run_tests
build\fifo_tests.exe
exit /b %errorlevel%

:run_asan
build\fifo_tests_asan.exe
exit /b %errorlevel%