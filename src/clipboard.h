#ifndef CLIPBOARD_H
#define CLIPBOARD_H

#include <stdbool.h>
#include <wchar.h>
#include <windows.h>

/* Запись текста с повторными попытками. При успехе система владеет памятью. */
bool     Clipboard_SetUnicodeText(HWND hwnd, const wchar_t *text);
/* Копия текста буфера (malloc); caller освобождает через FQ_FreeText. */
wchar_t *Clipboard_CopyUnicodeText(HWND hwnd);

#endif