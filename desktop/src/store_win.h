#ifndef HERMES_STORE_WIN_H
#define HERMES_STORE_WIN_H
#include <windows.h>
#include "core.h"
int store_path(wchar_t *out, size_t capacity);
cJSON *store_read(const wchar_t *path);
int store_write(const wchar_t *path, const cJSON *value);
#endif
