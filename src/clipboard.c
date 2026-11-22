#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif
#include <windows.h>
#include "clipboard.h"
#include <stdlib.h>
#include <string.h>

#define CLIP_ATTEMPTS 3
#define CLIP_RETRY_MS 15

bool Clipboard_SetUnicodeText(HWND hwnd, const wchar_t *text)
{
    if (!text) return false;
    size_t bytes = (wcslen(text) + 1) * sizeof(wchar_t);

    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!h) return false;
    void *p = GlobalLock(h);
    if (!p) { GlobalFree(h); return false; }
    memcpy(p, text, bytes);
    GlobalUnlock(h);

    for (int attempt = 0; attempt < CLIP_ATTEMPTS; ++attempt) {
        if (OpenClipboard(hwnd)) {
            EmptyClipboard();
            BOOL placed = (SetClipboardData(CF_UNICODETEXT, h) != NULL);
            CloseClipboard();
            if (placed) return true;   /* успех: память принадлежит системе */
            break;                     /* запись не удалась — освобождаем ниже */
        }
        Sleep(CLIP_RETRY_MS);
    }
    GlobalFree(h);
    return false;
}

wchar_t *Clipboard_CopyUnicodeText(HWND hwnd)
{
    for (int attempt = 0; attempt < CLIP_ATTEMPTS; ++attempt) {
        if (OpenClipboard(hwnd)) {
            wchar_t *result = NULL;
            if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
                HGLOBAL h = GetClipboardData(CF_UNICODETEXT);
                if (h) {
                    const wchar_t *src = (const wchar_t *)GlobalLock(h);
                    if (src) {
                        size_t bytes = (wcslen(src) + 1) * sizeof(wchar_t);
                        wchar_t *dst = (wchar_t *)malloc(bytes);
                        if (dst) { memcpy(dst, src, bytes); result = dst; }
                        GlobalUnlock(h);
                    }
                }
            }
            CloseClipboard();
            return result;
        }
        Sleep(CLIP_RETRY_MS);
    }
    return NULL;
}