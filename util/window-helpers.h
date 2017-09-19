#pragma once

#include "dstr.h"
#include "windows.h"

#ifdef __cplusplus
extern "C" {
#endif


enum window_priority {
	WINDOW_PRIORITY_CLASS,
	WINDOW_PRIORITY_TITLE,
	WINDOW_PRIORITY_EXE,
};

enum window_search_mode {
	INCLUDE_MINIMIZED,
	EXCLUDE_MINIMIZED
};

extern bool get_window_exe(struct dstr *name, HWND window);
extern void get_window_title(struct dstr *name, HWND hwnd);
extern void get_window_class(struct dstr *klass, HWND hwnd);
extern bool is_uwp_window(HWND hwnd);
extern HWND get_uwp_actual_window(HWND parent);

typedef bool (*add_window_cb)(const char *title, const char *klass,
		const char *exe);

#if 0
void fill_window_list(obs_property_t *p, enum window_search_mode mode,
		add_window_cb callback);
#endif

extern void build_window_strings(const char *str,
		char **klass,
		char **title,
		char **exe);

extern HWND find_window(enum window_search_mode mode,
		enum window_priority priority,
		const char *klass,
		const char *title,
		const char *exe);

#ifdef __cplusplus
 }
#endif