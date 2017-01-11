#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool is_app(HANDLE process);
wchar_t *get_app_sid(HANDLE process);
HANDLE open_app_mutex(const wchar_t *sid, const wchar_t *name);
HANDLE open_app_event(const wchar_t *sid, const wchar_t *name);
HANDLE open_app_map(const wchar_t *sid, const wchar_t *name);

#ifdef __cplusplus
}
#endif
