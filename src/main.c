#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif
#include <windows.h>
#include <shellapi.h>
#include <strsafe.h>
#include <stdlib.h>
#include <stdbool.h>
#include <wchar.h>

#include "fifo_queue.h"
#include "app_logic.h"
#include "settings.h"
#include "clipboard.h"

#define HOTKEY_TOGGLE_FIFO 1
#define HOTKEY_EXIT        2
#define HOTKEY_REMOVE_LAST 3

#define WM_TRAYICON  (WM_APP + 1)
#define WM_APP_PASTE (WM_APP + 2)

#define IDI_TRAYICON     1001
#define IDM_APP_NAME     2001
#define IDM_PAUSE        2002
#define IDM_SOUNDS       2003
#define IDM_SETTINGS     2004
#define IDM_EXIT         2005
#define IDM_CLEAR_QUEUE  2006

#define CLR_PAUSED RGB(80, 80, 80)
#define CLR_ACTIVE RGB(0, 128, 64)
#define CLR_TEXT   RGB(255, 255, 255)

typedef struct {
    HINSTANCE   inst;
    HWND        hwnd;
    HHOOK       hook;
    HANDLE      mutex;
    FifoQueue   queue;
    AppSettings settings;
    wchar_t     iniPath[MAX_PATH];
    DWORD       lastSeenSeq;
    DWORD       ownPasteSeq;
    UINT        taskbarCreatedMsg;
} AppState;

static AppState g_app;

/* ---------- DPI ---------- */
static void InitDpi(void)
{
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        SetProcessDPIAware();
}

static UINT GetTrayDpi(HWND hwnd)
{
    UINT dpi = GetDpiForWindow(hwnd);
    if (!dpi) dpi = GetDpiForSystem();
    if (!dpi) dpi = 96;
    return dpi;
}

/* ---------- Иконка ---------- */
static HICON CreateTextIcon(const wchar_t *text, COLORREF fg, COLORREF bg, UINT dpi)
{
    int w = GetSystemMetricsForDpi(SM_CXSMICON, dpi);
    int h = GetSystemMetricsForDpi(SM_CYSMICON, dpi);
    if (w <= 0 || h <= 0) return NULL;

    HDC screen = GetDC(NULL);
    if (!screen) return NULL;
    HDC mem = CreateCompatibleDC(screen);
    if (!mem) { ReleaseDC(NULL, screen); return NULL; }
    HBITMAP color = CreateCompatibleBitmap(screen, w, h);
    if (!color) { DeleteDC(mem); ReleaseDC(NULL, screen); return NULL; }

    HGDIOBJ oldBmp = SelectObject(mem, color);
    HBRUSH br = CreateSolidBrush(bg);
    if (br) { RECT r = { 0, 0, w, h }; FillRect(mem, &r, br); DeleteObject(br); }

    HGDIOBJ baseFont = GetCurrentObject(mem, OBJ_FONT);
    HFONT fitted = NULL;
    for (int ht = h - 2; ht >= 6; --ht) {
        HFONT f = CreateFontW(-ht, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        if (!f) continue;
        HGDIOBJ prev = SelectObject(mem, f);
        RECT rc = { 0, 0, w, h };
        DrawTextW(mem, text, -1, &rc, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
        if (rc.right - rc.left <= w - 1) { fitted = f; break; }
        SelectObject(mem, prev);
        DeleteObject(f);
    }
    if (!fitted) {
        fitted = CreateFontW(-6, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        if (fitted) SelectObject(mem, fitted);
    }

    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, fg);
    RECT dr = { 0, 0, w, h };
    DrawTextW(mem, text, -1, &dr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    if (baseFont) SelectObject(mem, baseFont);
    if (fitted) DeleteObject(fitted);
    SelectObject(mem, oldBmp);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);

    /* Явно обнулённая маска = полностью непрозрачная иконка (без UB). */
    int stride = ((w + 31) / 32) * 4;
    BYTE *bits = (BYTE *)calloc((size_t)stride * (size_t)h, 1);
    HBITMAP mask = bits ? CreateBitmap(w, h, 1, 1, bits) : NULL;
    free(bits);

    HICON icon = NULL;
    if (mask) {
        ICONINFO ii = { 0 };
        ii.fIcon = TRUE;
        ii.hbmMask = mask;
        ii.hbmColor = color;
        icon = CreateIconIndirect(&ii);
        DeleteObject(mask);
    }
    DeleteObject(color);
    return icon;
}

static void UpdateTrayIcon(HWND hwnd)
{
    wchar_t label[16];
    Logic_FormatIconText((int)FQ_Count(&g_app.queue), g_app.settings.fifoMode, label, 16);
    COLORREF bg = g_app.settings.fifoMode ? CLR_ACTIVE : CLR_PAUSED;

    HICON icon = CreateTextIcon(label, CLR_TEXT, bg, GetTrayDpi(hwnd));

    NOTIFYICONDATAW nid = { 0 };
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = IDI_TRAYICON;
    nid.uFlags = NIF_MESSAGE | NIF_TIP | (icon ? NIF_ICON : 0);
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = icon;
    StringCchPrintfW(nid.szTip, ARRAYSIZE(nid.szTip), L"FIFO Clipboard (%s)", label);

    if (!Shell_NotifyIconW(NIM_MODIFY, &nid)) {
        nid.uVersion = NOTIFYICON_VERSION_4;
        if (Shell_NotifyIconW(NIM_ADD, &nid))
            Shell_NotifyIconW(NIM_SETVERSION, &nid);
    }
    if (icon) DestroyIcon(icon);
}

static void RemoveTrayIcon(HWND hwnd)
{
    NOTIFYICONDATAW nid = { 0 };
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = IDI_TRAYICON;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

/* ---------- Меню ---------- */
static void ShowContextMenu(HWND hwnd)
{
    POINT pt;
    GetCursorPos(&pt);
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING | MF_GRAYED, IDM_APP_NAME, L"FIFO Clipboard");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING | (!g_app.settings.fifoMode ? MF_CHECKED : 0),
                IDM_PAUSE, L"Приостановка");
    AppendMenuW(hMenu, MF_STRING | (g_app.settings.sounds ? MF_CHECKED : 0),
                IDM_SOUNDS, L"Звуковые уведомления");
    AppendMenuW(hMenu, MF_STRING | (FQ_IsEmpty(&g_app.queue) ? MF_GRAYED : 0),
                IDM_CLEAR_QUEUE, L"Очистить очередь");
    AppendMenuW(hMenu, MF_STRING, IDM_SETTINGS, L"Параметры");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, IDM_EXIT, L"Закрыть");

    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, NULL);
    PostMessageW(hwnd, WM_NULL, 0, 0);   /* корректное закрытие меню (KB Q135788) */
    DestroyMenu(hMenu);
}

/* ---------- Вставка ---------- */
static void SendPaste(void)
{
    PasteInput plan[4];
    bool ctrlHeld = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    size_t n = Logic_BuildPasteInputs(ctrlHeld, plan, 4);
    if (!n) return;

    INPUT inputs[4];
    ZeroMemory(inputs, sizeof(inputs));
    for (size_t i = 0; i < n; ++i) {
        inputs[i].type = INPUT_KEYBOARD;
        inputs[i].ki.wVk = (WORD)plan[i].vk;
        inputs[i].ki.dwFlags = plan[i].keyUp ? KEYEVENTF_KEYUP : 0;
    }
    SendInput((UINT)n, inputs, sizeof(INPUT));
}

static void DoPasteFromQueue(HWND hwnd)
{
    wchar_t *text = FQ_PopFront(&g_app.queue);
    if (text) {
        if (Clipboard_SetUnicodeText(hwnd, text)) {
            g_app.ownPasteSeq = GetClipboardSequenceNumber();
            FQ_FreeText(text);
        } else {
            FQ_PushFrontOwned(&g_app.queue, text); /* откат: владение передано */
        }
        UpdateTrayIcon(hwnd);
    }
    SendPaste(); /* даже при пустой очереди — обычный Paste */
}

/* ---------- Хук (только дешёвые проверки) ---------- */
static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT *kb = (KBDLLHOOKSTRUCT *)lParam;
        static bool s_vDown = false;

        if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
            if (kb->vkCode == 'V') s_vDown = false;
        } else if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            if (kb->vkCode == 'V') {
                bool repeat = s_vDown;
                s_vDown = true;
                if (!repeat) {
                    bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
                    bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                    bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
                    bool injected = (kb->flags & LLKHF_INJECTED) != 0;
                    if (Logic_ShouldInterceptPaste(kb->vkCode, ctrl, shift, alt, injected,
                                                   g_app.settings.fifoMode,
                                                   FQ_Count(&g_app.queue))) {
                        PostMessageW(g_app.hwnd, WM_APP_PASTE, 0, 0);
                        return 1; /* блокируем оригинал, вставку синтезируем сами */
                    }
                }
            }
        }
    }
    return CallNextHookEx(g_app.hook, nCode, wParam, lParam);
}

/* ---------- Настройки ---------- */
static void SaveSettings(void)
{
    Settings_Save(g_app.iniPath, &g_app.settings);
}

static void ResolveIniPath(void)
{
    wchar_t exe[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        wchar_t *slash = wcsrchr(exe, L'\\');
        if (slash) {
            *(slash + 1) = L'\0';
            StringCchPrintfW(g_app.iniPath, MAX_PATH, L"%sfifo_clipboard.ini", exe);
        }
    }
    if (g_app.iniPath[0] == L'\0')
        StringCchCopyW(g_app.iniPath, MAX_PATH, L"fifo_clipboard.ini");

    /* Если каталог exe недоступен для записи — уходим в APPDATA. */
    if (!Settings_Save(g_app.iniPath, &g_app.settings)) {
        wchar_t appdata[MAX_PATH];
        DWORD k = GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
        if (k > 0 && k < MAX_PATH)
            StringCchPrintfW(g_app.iniPath, MAX_PATH, L"%s\\fifo_clipboard.ini", appdata);
    }
}

static void ToggleFifo(HWND hwnd)
{
    g_app.settings.fifoMode = !g_app.settings.fifoMode;
    if (g_app.settings.sounds)
        MessageBeep(g_app.settings.fifoMode ? MB_ICONINFORMATION : MB_ICONERROR);
    SaveSettings();
    UpdateTrayIcon(hwnd);
}

/* ---------- Оконная процедура ---------- */
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == g_app.taskbarCreatedMsg && msg != 0) {
        UpdateTrayIcon(hwnd);   /* explorer перезапущен — возвращаем иконку */
        return 0;
    }

    switch (msg) {
    case WM_TRAYICON:
        switch (LOWORD(lParam)) {
        case WM_CONTEXTMENU:
        case NIN_SELECT:
        case NIN_KEYSELECT:
            ShowContextMenu(hwnd);
            break;
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDM_PAUSE:
            ToggleFifo(hwnd);
            break;
        case IDM_SOUNDS:
            g_app.settings.sounds = !g_app.settings.sounds;
            SaveSettings();
            break;
        case IDM_CLEAR_QUEUE:
            FQ_Clear(&g_app.queue);
            if (g_app.settings.sounds) MessageBeep(MB_OK);
            UpdateTrayIcon(hwnd);
            break;
        case IDM_SETTINGS:
            MessageBoxW(hwnd,
                L"Горячие клавиши:\n"
                L"Alt + Shift + F — Вкл/Выкл FIFO\n"
                L"Alt + Shift + Z — Удалить посл. скопированный элемент\n"
                L"Alt + Shift + Q — Выход",
                L"Параметры", MB_OK | MB_ICONINFORMATION);
            break;
        case IDM_EXIT:
            DestroyWindow(hwnd);
            break;
        }
        return 0;

    case WM_HOTKEY:
        if (wParam == HOTKEY_TOGGLE_FIFO) {
            ToggleFifo(hwnd);
        } else if (wParam == HOTKEY_REMOVE_LAST) {
            if (g_app.settings.fifoMode) {
                wchar_t *t = FQ_PopBack(&g_app.queue);
                if (t) {
                    FQ_FreeText(t);
                    if (g_app.settings.sounds) MessageBeep(MB_OK);
                    UpdateTrayIcon(hwnd);
                } else if (g_app.settings.sounds) {
                    MessageBeep(MB_ICONHAND);
                }
            }
        } else if (wParam == HOTKEY_EXIT) {
            if (g_app.settings.sounds) MessageBeep(MB_OK);
            DestroyWindow(hwnd);
        }
        return 0;

    case WM_APP_PASTE:
        DoPasteFromQueue(hwnd);
        return 0;

    case WM_CLIPBOARDUPDATE: {
        DWORD seq = GetClipboardSequenceNumber();
        ClipAction action = Logic_ClassifyClipboardUpdate(seq, g_app.lastSeenSeq, g_app.ownPasteSeq);
        g_app.lastSeenSeq = seq;
        if (action != CLIP_PROCESS) return 0;
        if (!g_app.settings.fifoMode) return 0;

        wchar_t *text = Clipboard_CopyUnicodeText(hwnd);
        if (text) {
            FQ_Push(&g_app.queue, text);
            FQ_FreeText(text);
            UpdateTrayIcon(hwnd);
        }
        return 0;
    }

    case WM_DPICHANGED:
        UpdateTrayIcon(hwnd);
        return 0;

    case WM_DESTROY:
        RemoveClipboardFormatListener(hwnd);
        UnregisterHotKey(hwnd, HOTKEY_TOGGLE_FIFO);
        UnregisterHotKey(hwnd, HOTKEY_REMOVE_LAST);
        UnregisterHotKey(hwnd, HOTKEY_EXIT);
        RemoveTrayIcon(hwnd);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/* ---------- Точка входа ---------- */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    (void)hPrevInstance; (void)lpCmdLine; (void)nCmdShow;

    g_app.inst = hInstance;
    FQ_Init(&g_app.queue);
    Settings_Defaults(&g_app.settings);
    InitDpi();

    g_app.mutex = CreateMutexW(NULL, TRUE, L"Local\\FifoClipboard.SingleInstance");
    if (!g_app.mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (g_app.mutex) CloseHandle(g_app.mutex);
        return 0;
    }

    ResolveIniPath();
    Settings_Load(g_app.iniPath, &g_app.settings);

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"FifoClipboardClass";
    if (!RegisterClassW(&wc)) { CloseHandle(g_app.mutex); return 1; }

    /* Скрытое ОБЫЧНОЕ окно (не message-only): может быть foreground и получает broadcast. */
    g_app.hwnd = CreateWindowExW(0, wc.lpszClassName, L"FIFO Clipboard",
                                 WS_OVERLAPPEDWINDOW, 0, 0, 0, 0,
                                 NULL, NULL, hInstance, NULL);
    if (!g_app.hwnd) { CloseHandle(g_app.mutex); return 1; }

    g_app.taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");
    g_app.lastSeenSeq = GetClipboardSequenceNumber();

    UpdateTrayIcon(g_app.hwnd);

    if (!AddClipboardFormatListener(g_app.hwnd))
        MessageBoxW(g_app.hwnd, L"Не удалось подписаться на изменения буфера обмена.",
                    L"FIFO Clipboard", MB_ICONWARNING);

    bool hkOk = true;
    hkOk &= RegisterHotKey(g_app.hwnd, HOTKEY_TOGGLE_FIFO, MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'F') != 0;
    hkOk &= RegisterHotKey(g_app.hwnd, HOTKEY_REMOVE_LAST, MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'Z') != 0;
    hkOk &= RegisterHotKey(g_app.hwnd, HOTKEY_EXIT, MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'Q') != 0;
    if (!hkOk)
        MessageBoxW(g_app.hwnd, L"Часть горячих клавиш занята другим приложением.",
                    L"FIFO Clipboard", MB_ICONWARNING);

    g_app.hook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, hInstance, 0);
    if (!g_app.hook)
        MessageBoxW(g_app.hwnd,
                    L"Не удалось установить клавиатурный хук.\nВставка из очереди по Ctrl+V недоступна.",
                    L"FIFO Clipboard", MB_ICONWARNING);

    MSG msg;
    BOOL ret;
    while ((ret = GetMessageW(&msg, NULL, 0, 0)) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_app.hook) UnhookWindowsHookEx(g_app.hook);
    FQ_Clear(&g_app.queue);
    Settings_Save(g_app.iniPath, &g_app.settings);
    CloseHandle(g_app.mutex);
    return (ret == -1) ? 1 : 0;
}