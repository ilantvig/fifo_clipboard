#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    bool fifoMode;
    bool sounds;
} AppSettings;

void Settings_Defaults(AppSettings *s);
/* Если файла нет — возвращает true и значения по умолчанию. */
bool Settings_Load(const wchar_t *path, AppSettings *s);
bool Settings_Save(const wchar_t *path, const AppSettings *s);

#endif