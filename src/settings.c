#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif
#include <windows.h>
#include "settings.h"

void Settings_Defaults(AppSettings *s)
{
    if (!s) return;
    s->fifoMode = false;
    s->sounds   = false;
}

bool Settings_Load(const wchar_t *path, AppSettings *s)
{
    if (!s) return false;
    Settings_Defaults(s);
    if (!path) return false;
    s->fifoMode = GetPrivateProfileIntW(L"General", L"FifoMode", 0, path) != 0;
    s->sounds   = GetPrivateProfileIntW(L"General", L"Sounds",   0, path) != 0;
    return true;
}

bool Settings_Save(const wchar_t *path, const AppSettings *s)
{
    if (!path || !s) return false;
    BOOL ok = WritePrivateProfileStringW(L"General", L"FifoMode",
                                         s->fifoMode ? L"1" : L"0", path);
    ok &= WritePrivateProfileStringW(L"General", L"Sounds",
                                     s->sounds ? L"1" : L"0", path);
    return ok != 0;
}