#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000004   /* NTDDI_WIN10_RS3 */
#endif
#include <windows.h>
#include "clipboard.h"
#include "fifo_queue.h"
#include <stdlib.h>
#include <string.h>

#define CLIP_ATTEMPTS 3
#define CLIP_RETRY_MS 15
#define CLIP_MAX_BYTES FIFO_MAX_ITEM_BYTES   /* fifo_queue.h */


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
            BOOL placed = FALSE;
            if (EmptyClipboard())
                placed = (SetClipboardData(CF_UNICODETEXT, h) != NULL);
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
                SIZE_T  cap = h ? GlobalSize(h) : 0;
                if (cap >= sizeof(wchar_t) && cap <= CLIP_MAX_BYTES) {
                    const wchar_t *src = (const wchar_t *)GlobalLock(h);
                    if (src) {
                        size_t maxChars = cap / sizeof(wchar_t), len = 0;
                        while (len < maxChars && src[len]) ++len;      /* никогда за GlobalSize */
                        if (len < maxChars) {                          /* терминатор найден */
                            size_t bytes = (len + 1) * sizeof(wchar_t);
                            result = (wchar_t *)malloc(bytes);
                            if (result) memcpy(result, src, bytes);
                        }
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