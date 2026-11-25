@echo off
setlocal
set SRC=src\main.c src\fifo_queue.c src\app_logic.c src\settings.c src\clipboard.c
set TESTSRC=tests\test_main.c src\fifo_queue.c src\app_logic.c src\settings.c src\clipboard.c
set CC=clang
set CFLAGS=-std=c11 -Wall -Wextra -O2

if "%1"=="tests" (
    %CC% %CFLAGS% %TESTSRC% -o fifo_tests.exe -luser32 -lkernel32 %2 -Isrc
    goto :eof
)
if "%1"=="msvc" (
    cl /nologo /W4 /utf-8 /std:c11 %SRC% /link user32.lib gdi32.lib shell32.lib /SUBSYSTEM:WINDOWS /OUT:fifo_clipboard.exe
    goto :eof
)
if "%1"=="msvc-tests" (
    cl /nologo /W4 /utf-8 /std:c11 %TESTSRC% /link user32.lib /OUT:fifo_tests.exe
    goto :eof
)
%CC% %CFLAGS% %SRC% -o fifo_clipboard.exe -mwindows -luser32 -lgdi32 -lshell32
endlocal
