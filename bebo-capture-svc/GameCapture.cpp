#include "GameCapture.h"
// TODO - should not need path:

#include <strsafe.h>
#include <tchar.h>
#include <windows.h>
#include <dxgi.h>
#include "graphics-hook-info.h"
#include "bmem.h"
#include "dstr.h"
#include "app-helpers.h"
#include "platform.h"
#include "threading.h"
#include "obfuscate.h"
#include "nt-stuff.h"
#include "inject-library.h"
#include "DibHelper.h"
#include "window-helpers.h"
#include "ipc-util/pipe.h"

#define do_log(level, format, ...) \
	LocalOutput("[game-capture: '%s'] " format, "test", ##__VA_ARGS__)
#define warn(format, ...)  do_log(LOG_WARNING, format, ##__VA_ARGS__)
#define info(format, ...)  do_log(LOG_INFO,    format, ##__VA_ARGS__)
#define debug(format, ...) do_log(LOG_DEBUG,   format, ##__VA_ARGS__)

#define STOP_BEING_BAD \
	"  This is most likely due to security software. Please make sure " \
        "that the Bebo Capture installation folder is excluded/ignored in the "      \
        "settings of the security software you are using."

extern "C" {
	char *bebo_find_file(const char *file) {
		char * d = "C:\\Users\\fpn\\bebo-capture\\x64\\Debug\\";
		char * result = (char *) bmalloc(strlen(file) + strlen(d));
		strcpy(result, d);
		strncat(result, file, strlen(file));
		return result;
		// TODO free result somewhere?
	}
	struct graphics_offsets offsets32 = {0};
	struct graphics_offsets offsets64 = {0};
}

enum capture_mode {
	CAPTURE_MODE_ANY,
	CAPTURE_MODE_WINDOW,
	CAPTURE_MODE_HOTKEY
};

struct game_capture_config {
	char                          *title;
	char                          *klass;
	char                          *executable;
	enum window_priority          priority;
	enum capture_mode             mode;
	uint32_t                      scale_cx;
	uint32_t                      scale_cy;
	bool                          cursor : 1;
	bool                          force_shmem : 1;
	bool                          force_scaling : 1;
	bool                          allow_transparency : 1;
	bool                          limit_framerate : 1;
	bool                          capture_overlays : 1;
	bool                          anticheat_hook : 1;
};

struct game_capture {
//	obs_source_t                  *source;

//	struct cursor_data            cursor_data;
	HANDLE                        injector_process;
	uint32_t                      cx;
	uint32_t                      cy;
	uint32_t                      pitch;
	DWORD                         process_id;
	DWORD                         thread_id;
	HWND                          next_window;
	HWND                          window;
	float                         retry_time;
	float                         fps_reset_time;
	float                         retry_interval;
	struct dstr                   title;
	struct dstr                   klass;
	struct dstr                   executable;
	enum window_priority          priority;
//	obs_hotkey_pair_id            hotkey_pair;
	volatile long                 hotkey_window;
	volatile bool                 deactivate_hook;
	volatile bool                 activate_hook_now;
	bool                          wait_for_target_startup : 1;
	bool                          showing : 1;
	bool                          active : 1;
	bool                          capturing : 1;
	bool                          activate_hook : 1;
	bool                          process_is_64bit : 1;
	bool                          error_acquiring : 1;
	bool                          dwm_capture : 1;
	bool                          initial_config : 1;
	bool                          convert_16bit : 1;
	bool                          is_app : 1;

	struct game_capture_config    config;

	ipc_pipe_server_t             pipe;
//	gs_texture_t                  *texture;
	struct hook_info              *global_hook_info;
	HANDLE                        keepalive_thread;
	DWORD                         keepalive_thread_id;
	HANDLE                        hook_init;
	HANDLE                        hook_restart;
	HANDLE                        hook_stop;
	HANDLE                        hook_ready;
	HANDLE                        hook_exit;
	HANDLE                        hook_data_map;
	HANDLE                        global_hook_info_map;
	HANDLE                        target_process;
	HANDLE                        texture_mutexes[2];
	wchar_t                       *app_sid;
	int                           retrying;

	union {
		struct {
			struct shmem_data *shmem_data;
			uint8_t *texture_buffers[2];
		};

		struct shtex_data *shtex_data;
		void *data;
	};

	void(*copy_texture)(struct game_capture*);
};




static inline int inject_library(HANDLE process, const wchar_t *dll)
{
	return inject_library_obf(process, dll,
			"D|hkqkW`kl{k\\osofj", 0xa178ef3655e5ade7,
			"[uawaRzbhh{tIdkj~~", 0x561478dbd824387c,
			"[fr}pboIe`dlN}", 0x395bfbc9833590fd,
			"\\`zs}gmOzhhBq", 0x12897dd89168789a,
			"GbfkDaezbp~X", 0x76aff7238788f7db);
}


static inline bool use_anticheat(struct game_capture *gc)
{
	return gc->config.anticheat_hook && !gc->is_app;
}

static inline HANDLE open_mutex_plus_id(struct game_capture *gc,
	const wchar_t *name, DWORD id)
{
	wchar_t new_name[64];
	_snwprintf(new_name, 64, L"%s%lu", name, id);
	return gc->is_app
		? open_app_mutex(gc->app_sid, new_name)
		: open_mutex(new_name);
}

static inline HANDLE open_mutex_gc(struct game_capture *gc,
	const wchar_t *name)
{
	return open_mutex_plus_id(gc, name, gc->process_id);
}

static inline HANDLE open_event_plus_id(struct game_capture *gc,
	const wchar_t *name, DWORD id)
{
	wchar_t new_name[64];
	_snwprintf(new_name, 64, L"%s%lu", name, id);
	return gc->is_app
		? open_app_event(gc->app_sid, new_name)
		: open_event(new_name);
}

static inline HANDLE open_event_gc(struct game_capture *gc,
	const wchar_t *name)
{
	return open_event_plus_id(gc, name, gc->process_id);
}

static inline HANDLE open_map_plus_id(struct game_capture *gc,
	const wchar_t *name, DWORD id)
{
	wchar_t new_name[64];
	_snwprintf(new_name, 64, L"%s%lu", name, id);

	debug("map id: %S", new_name);

	return gc->is_app
		? open_app_map(gc->app_sid, new_name)
		: OpenFileMappingW(GC_MAPPING_FLAGS, false, new_name);
}

static inline HANDLE open_hook_info(struct game_capture *gc)
{
	return open_map_plus_id(gc, SHMEM_HOOK_INFO, gc->process_id);
}
static struct game_capture *game_capture_create()
{
	struct game_capture *gc = (struct game_capture*) bzalloc(sizeof(*gc));

//	wait_for_hook_initialization();

	//gc->source = source;
	gc->initial_config = true;
// 	gc->retry_interval = DEFAULT_RETRY_INTERVAL;
// 	gc->hotkey_pair = obs_hotkey_pair_register_source(
// 		gc->source,
// 		HOTKEY_START, TEXT_HOTKEY_START,
// 		HOTKEY_STOP, TEXT_HOTKEY_STOP,
// 		hotkey_start, hotkey_stop, gc, gc);
// 
// 	game_capture_update(gc, settings);
	return gc;
}

static void close_handle(HANDLE *p_handle)
{
	HANDLE handle = *p_handle;
	if (handle) {
		if (handle != INVALID_HANDLE_VALUE)
			CloseHandle(handle);
		*p_handle = NULL;
	}
}

static inline HMODULE kernel32(void)
{
	static HMODULE kernel32_handle = NULL;
	if (!kernel32_handle)
		kernel32_handle = GetModuleHandleW(L"kernel32");
	return kernel32_handle;
}

static inline HANDLE open_process(DWORD desired_access, bool inherit_handle,
		DWORD process_id)
{
	static HANDLE (WINAPI *open_process_proc)(DWORD, BOOL, DWORD) = NULL;
	if (!open_process_proc)
		open_process_proc = (HANDLE(__stdcall *) (DWORD, BOOL, DWORD)) get_obfuscated_func(kernel32(),
				"NuagUykjcxr", 0x1B694B59451ULL);

	return open_process_proc(desired_access, inherit_handle, process_id);
}

static void setup_window(struct game_capture *gc, HWND window)
{
	HANDLE hook_restart;
	HANDLE process;

	GetWindowThreadProcessId(window, &gc->process_id);
	if (gc->process_id) {
		process = open_process(PROCESS_QUERY_INFORMATION,
			false, gc->process_id);
		if (process) {
			gc->is_app = is_app(process);
			if (gc->is_app) {
				gc->app_sid = get_app_sid(process);
			}
			CloseHandle(process);
		}
	}

	/* do not wait if we're re-hooking a process */
	hook_restart = open_event_gc(gc, EVENT_CAPTURE_RESTART);
	if (hook_restart) {
		gc->wait_for_target_startup = false;
		CloseHandle(hook_restart);
	}

	/* otherwise if it's an unhooked process, always wait a bit for the
	 * target process to start up before starting the hook process;
	 * sometimes they have important modules to load first or other hooks
	 * (such as steam) need a little bit of time to load.  ultimately this
	 * helps prevent crashes */
	if (gc->wait_for_target_startup) {
		gc->retry_interval = 3.0f;
		gc->wait_for_target_startup = false;
	} else {
		gc->next_window = window;
	}
}
static void get_fullscreen_window(struct game_capture *gc)
{
	HWND window = GetForegroundWindow();
	MONITORINFO mi = {0};
	HMONITOR monitor;
	DWORD styles;
	RECT rect;

	gc->next_window = NULL;

	if (!window) {
		return;
	}
	if (!GetWindowRect(window, &rect)) {
		return;
	}

	/* ignore regular maximized windows */
	styles = (DWORD)GetWindowLongPtr(window, GWL_STYLE);
	if ((styles & WS_MAXIMIZE) != 0 && (styles & WS_BORDER) != 0) {
		return;
	}

	monitor = MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST);
	if (!monitor) {
		return;
	}

	mi.cbSize = sizeof(mi);
	if (!GetMonitorInfo(monitor, &mi)) {
		return;
	}

	if (rect.left   == mi.rcMonitor.left   &&
	    rect.right  == mi.rcMonitor.right  &&
	    rect.bottom == mi.rcMonitor.bottom &&
	    rect.top    == mi.rcMonitor.top) {
		setup_window(gc, window);
	} else {
		gc->wait_for_target_startup = true;
	}
}

static void get_selected_window(struct game_capture *gc)
{
	HWND window;

	if (dstr_cmpi(&gc->klass, "dwm") == 0) {
		wchar_t class_w[512];
		os_utf8_to_wcs(gc->klass.array, 0, class_w, 512);
		window = FindWindowW(class_w, NULL);
	} else {
		window = find_window(INCLUDE_MINIMIZED,
				gc->priority,
				gc->klass.array,
				gc->title.array,
				gc->executable.array);
	}

	if (window) {
		setup_window(gc, window);
	} else {
		gc->wait_for_target_startup = true;
	}
}

static void stop_capture(struct game_capture *gc)
{
	// FIXME
}

static inline bool hook_direct(struct game_capture *gc,
		const char *hook_path_rel)
{
	wchar_t hook_path_abs_w[MAX_PATH];
	wchar_t *hook_path_rel_w;
	wchar_t *path_ret;
	HANDLE process;
	int ret;

	os_utf8_to_wcs_ptr(hook_path_rel, 0, &hook_path_rel_w);
	if (!hook_path_rel_w) {
		warn("hook_direct: could not convert string");
		return false;
	}

	path_ret = _wfullpath(hook_path_abs_w, hook_path_rel_w, MAX_PATH);
	bfree(hook_path_rel_w);

	if (path_ret == NULL) {
		warn("hook_direct: could not make absolute path");
		return false;
	}

	process = open_process(PROCESS_ALL_ACCESS, false, gc->process_id);
	if (!process) {
		warn("hook_direct: could not open process: %s (%lu)",
				gc->config.executable, GetLastError());
		return false;
	}

	ret = inject_library(process, hook_path_abs_w);
	CloseHandle(process);

	if (ret != 0) {
		warn("hook_direct: inject failed: %d", ret);
		return false;
	}

	return true;
}

static const char *blacklisted_exes[] = {
	"explorer",
	"steam",
	"battle.net",
	"galaxyclient",
	"skype",
	"uplay",
	"origin",
	"devenv",
	"taskmgr",
	"systemsettings",
	"applicationframehost",
	"cmd",
	"shellexperiencehost",
	"winstore.app",
	"searchui",
	NULL
};
static bool is_blacklisted_exe(const char *exe)
{
	char cur_exe[MAX_PATH];

	if (!exe)
		return false;

	for (const char **vals = blacklisted_exes; *vals; vals++) {
		strcpy(cur_exe, *vals);
		strcat(cur_exe, ".exe");

		if (strcmpi(cur_exe, exe) == 0)
			return true;
	}

	return false;
}

static bool target_suspended(struct game_capture *gc)
{
	return thread_is_suspended(gc->process_id, gc->thread_id);
}

static inline bool is_64bit_windows(void)
{
#ifdef _WIN64
	return true;
#else
	BOOL x86 = false;
	bool success = !!IsWow64Process(GetCurrentProcess(), &x86);
	return success && !!x86;
#endif
}

static inline bool is_64bit_process(HANDLE process)
{
	BOOL x86 = true;
	if (is_64bit_windows()) {
		bool success = !!IsWow64Process(process, &x86);
		if (!success) {
			return false;
		}
	}

	return !x86;
}

static inline bool open_target_process(struct game_capture *gc)
{
	gc->target_process = open_process(
			PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
			false, gc->process_id);
	if (!gc->target_process) {
		warn("could not open process: %s", gc->config.executable);
		return false;
	}

	gc->process_is_64bit = is_64bit_process(gc->target_process);
	gc->is_app = is_app(gc->target_process);
	if (gc->is_app) {
		gc->app_sid = get_app_sid(gc->target_process);
	}
	return true;
}

static bool check_file_integrity(struct game_capture *gc, const char *file,
		const char *name)
{
	DWORD error;
	HANDLE handle;
	wchar_t *w_file = NULL;

	if (!file || !*file) {
		warn("Game capture %s not found.", STOP_BEING_BAD, name);
		return false;
	}

	if (!os_utf8_to_wcs_ptr(file, 0, &w_file)) {
		warn("Could not convert file name to wide string");
		return false;
	}

	handle = CreateFileW(w_file, GENERIC_READ | GENERIC_EXECUTE,
			FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);

	bfree(w_file);

	if (handle != INVALID_HANDLE_VALUE) {
		CloseHandle(handle);
		return true;
	}

	error = GetLastError();
	if (error == ERROR_FILE_NOT_FOUND) {
		warn("Game capture file '%s' not found."
				STOP_BEING_BAD, file);
	} else if (error == ERROR_ACCESS_DENIED) {
		warn("Game capture file '%s' could not be loaded."
				STOP_BEING_BAD, file);
	} else {
		warn("Game capture file '%s' could not be loaded: %lu."
				STOP_BEING_BAD, file, error);
	}

	return false;
}

static inline bool create_inject_process(struct game_capture *gc,
		const char *inject_path, const char *hook_dll)
{
	wchar_t *command_line_w = (wchar_t *) malloc(4096 * sizeof(wchar_t));
	wchar_t *inject_path_w;
	wchar_t *hook_dll_w;
	bool anti_cheat = use_anticheat(gc);
	PROCESS_INFORMATION pi = {0};
	STARTUPINFO si = {0};
	bool success = false;

	os_utf8_to_wcs_ptr(inject_path, 0, &inject_path_w);
	os_utf8_to_wcs_ptr(hook_dll, 0, &hook_dll_w);

	si.cb = sizeof(si);

	swprintf(command_line_w, 4096, L"\"%s\" \"%s\" %lu %lu",
			inject_path_w, hook_dll_w,
			(unsigned long)anti_cheat,
			anti_cheat ? gc->thread_id : gc->process_id);

	success = !!CreateProcessW(inject_path_w, command_line_w, NULL, NULL,
			false, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
	if (success) {
		CloseHandle(pi.hThread);
		gc->injector_process = pi.hProcess;
	} else {
		warn("Failed to create inject helper process: %lu",
				GetLastError());
	}

	free(command_line_w);
	bfree(inject_path_w);
	bfree(hook_dll_w);
	return success;
}

static inline bool inject_hook(struct game_capture *gc)
{
	bool matching_architecture;
	bool success = false;
	const char *hook_dll;
	char *inject_path;
	char *hook_path;

	if (gc->process_is_64bit) {
		hook_dll = "graphics-hook64.dll";
		inject_path = bebo_find_file("inject-helper64.exe");
	} else {
		hook_dll = "graphics-hook32.dll";
		inject_path = bebo_find_file("inject-helper32.exe");
	}

	hook_path = bebo_find_file(hook_dll);

	if (!check_file_integrity(gc, inject_path, "inject helper")) {
		goto cleanup;
	}
	if (!check_file_integrity(gc, hook_path, "graphics hook")) {
		goto cleanup;
	}

#ifdef _WIN64
	matching_architecture = gc->process_is_64bit;
#else
	matching_architecture = !gc->process_is_64bit;
#endif

	if (matching_architecture && !use_anticheat(gc)) {
		info("using direct hook");
		success = hook_direct(gc, hook_path);
	} else {
		info("using helper (%s hook)", use_anticheat(gc) ?
				"compatibility" : "direct");
		success = create_inject_process(gc, inject_path, hook_dll);
	}

cleanup:
	bfree(inject_path);
	bfree(hook_path);
	return success;
}

struct keepalive_data {
	struct game_capture *gc;
	HANDLE initialized;
};

#define DEF_FLAGS (WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS)

static DWORD WINAPI keepalive_window_thread(struct keepalive_data *data)
{
	HANDLE initialized = data->initialized;
	struct game_capture *gc = data->gc;
	wchar_t new_name[64];
	WNDCLASSW wc;
	HWND window;
	MSG msg;

	_snwprintf(new_name, sizeof(new_name), L"%s%lu",
			WINDOW_HOOK_KEEPALIVE, gc->process_id);

	memset(&wc, 0, sizeof(wc));
	wc.style = CS_OWNDC;
	wc.hInstance = GetModuleHandleW(NULL);
	wc.lpfnWndProc = (WNDPROC)DefWindowProc;
	wc.lpszClassName = new_name;

	if (!RegisterClass(&wc)) {
		warn("Failed to create keepalive window class: %lu",
				GetLastError());
		return 0;
	}

	window = CreateWindowExW(0, new_name, NULL, DEF_FLAGS, 0, 0, 1, 1,
			NULL, NULL, wc.hInstance, NULL);
	if (!window) {
		warn("Failed to create keepalive window: %lu",
				GetLastError());
		return 0;
	}

	SetEvent(initialized);

	while (GetMessage(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	DestroyWindow(window);
	UnregisterClassW(new_name, wc.hInstance);

	return 0;
}

static inline bool init_keepalive(struct game_capture *gc)
{
	struct keepalive_data data;
	HANDLE initialized = CreateEvent(NULL, false, false, NULL);

	data.gc = gc;
	data.initialized = initialized;

	gc->keepalive_thread = CreateThread(NULL, 0,
			(LPTHREAD_START_ROUTINE)keepalive_window_thread,
			&data, 0, &gc->keepalive_thread_id);
	if (!gc->keepalive_thread) {
		warn("Failed to create keepalive window thread: %lu",
				GetLastError());
		return false;
	}

	WaitForSingleObject(initialized, INFINITE);
	CloseHandle(initialized);

	return true;
}

static inline bool init_texture_mutexes(struct game_capture *gc)
{
	gc->texture_mutexes[0] = open_mutex_gc(gc, MUTEX_TEXTURE1);
	gc->texture_mutexes[1] = open_mutex_gc(gc, MUTEX_TEXTURE2);

	if (!gc->texture_mutexes[0] || !gc->texture_mutexes[1]) {
		DWORD error = GetLastError();
		if (error == 2) {
			if (!gc->retrying) {
				gc->retrying = 2;
				info("hook not loaded yet, retrying..");
			}
		} else {
			warn("failed to open texture mutexes: %lu",
					GetLastError());
		}
		return false;
	}

	return true;
}
static void pipe_log(void *param, uint8_t *data, size_t size)
{
//	struct game_capture *gc = param;
	if (data && size)
		info("%s", data);
}

static inline bool init_pipe(struct game_capture *gc)
{
	char name[64];
	sprintf(name, "%s%lu", PIPE_NAME, gc->process_id);

	if (!ipc_pipe_server_start(&gc->pipe, name, pipe_log, gc)) {
		warn("init_pipe: failed to start pipe");
		return false;
	}

	return true;
}

static inline void reset_frame_interval(struct game_capture *gc)
{
	// FIXME
#if 0
	struct obs_video_info ovi;
	uint64_t interval = 0;

	if (obs_get_video_info(&ovi)) {
		interval = ovi.fps_den * 1000000000ULL / ovi.fps_num;

		/* Always limit capture framerate to some extent.  If a game
		 * running at 900 FPS is being captured without some sort of
		 * limited capture interval, it will dramatically reduce
		 * performance. */
		if (!gc->config.limit_framerate)
			interval /= 2;
	}

	gc->global_hook_info->frame_interval = interval;
#endif 
}

static inline bool init_hook_info(struct game_capture *gc)
{
	gc->global_hook_info_map = open_hook_info(gc);
	if (!gc->global_hook_info_map) {
		warn("init_hook_info: get_hook_info failed: %lu",
				GetLastError());
		return false;
	}

	gc->global_hook_info = (hook_info *) MapViewOfFile(gc->global_hook_info_map,
			FILE_MAP_ALL_ACCESS, 0, 0,
			sizeof(*gc->global_hook_info));
	if (!gc->global_hook_info) {
		warn("init_hook_info: failed to map data view: %lu",
				GetLastError());
		return false;
	}

	gc->global_hook_info->offsets = gc->process_is_64bit ?
		offsets64 : offsets32;
	gc->global_hook_info->capture_overlay = gc->config.capture_overlays;
	gc->global_hook_info->force_shmem = gc->config.force_shmem;
	gc->global_hook_info->use_scale = gc->config.force_scaling;
	gc->global_hook_info->cx = gc->config.scale_cx;
	gc->global_hook_info->cy = gc->config.scale_cy;
	reset_frame_interval(gc);

#if 0 // FIXME
	obs_enter_graphics();
	if (!gs_shared_texture_available())
		gc->global_hook_info->force_shmem = true;
	obs_leave_graphics();

	obs_enter_graphics();
	if (!gs_shared_texture_available())
		gc->global_hook_info->force_shmem = true;
	obs_leave_graphics();
#endif
	return true;
}

static inline bool init_events(struct game_capture *gc)
{
	if (!gc->hook_restart) {
		gc->hook_restart = open_event_gc(gc, EVENT_CAPTURE_RESTART);
		if (!gc->hook_restart) {
			warn("init_events: failed to get hook_restart "
			     "event: %lu", GetLastError());
			return false;
		}
	}

	if (!gc->hook_stop) {
		gc->hook_stop = open_event_gc(gc, EVENT_CAPTURE_STOP);
		if (!gc->hook_stop) {
			warn("init_events: failed to get hook_stop event: %lu",
					GetLastError());
			return false;
		}
	}

	if (!gc->hook_init) {
		gc->hook_init = open_event_gc(gc, EVENT_HOOK_INIT);
		if (!gc->hook_init) {
			warn("init_events: failed to get hook_init event: %lu",
					GetLastError());
			return false;
		}
	}

	if (!gc->hook_ready) {
		gc->hook_ready = open_event_gc(gc, EVENT_HOOK_READY);
		if (!gc->hook_ready) {
			warn("init_events: failed to get hook_ready event: %lu",
					GetLastError());
			return false;
		}
	}

	if (!gc->hook_exit) {
		gc->hook_exit = open_event_gc(gc, EVENT_HOOK_EXIT);
		if (!gc->hook_exit) {
			warn("init_events: failed to get hook_exit event: %lu",
					GetLastError());
			return false;
		}
	}

	return true;
}

/* if there's already a hook in the process, then signal and start */
static inline bool attempt_existing_hook(struct game_capture *gc)
{
	gc->hook_restart = open_event_gc(gc, EVENT_CAPTURE_RESTART);
	if (gc->hook_restart) {
		debug("existing hook found, signaling process: %s",
				gc->config.executable);
		SetEvent(gc->hook_restart);
		return true;
	}

	return false;
}


static bool init_hook(struct game_capture *gc)
{
	struct dstr exe = {0};
	bool blacklisted_process = false;

	if (gc->config.mode == CAPTURE_MODE_ANY) {
		if (get_window_exe(&exe, gc->next_window)) {
			info("attempting to hook fullscreen process: %s",
					exe.array);
		}
	} else {
		if (get_window_exe(&exe, gc->next_window)) {
			info("attempting to hook process: %s", exe.array);
		}
	}

	blacklisted_process = is_blacklisted_exe(exe.array);
	if (blacklisted_process)
		info("cannot capture %s due to being blacklisted", exe.array);
	dstr_free(&exe);

	if (blacklisted_process) {
		return false;
	}
	if (target_suspended(gc)) {
		return false;
	}
	if (!open_target_process(gc)) {
		return false;
	}
	if (!init_keepalive(gc)) {
		return false;
	}
	if (!init_pipe(gc)) {
		return false;
	}
	if (!attempt_existing_hook(gc)) {
		if (!inject_hook(gc)) {
			return false;
		}
	}
	if (!init_texture_mutexes(gc)) {
		return false;
	}

	if (!init_hook_info(gc)) {
		return false;
	}

	if (!init_events(gc)) {
		return false;
	}

	SetEvent(gc->hook_init);

	gc->window = gc->next_window;
	gc->next_window = NULL;
	gc->active = true;
	gc->retrying = 0;
	return true;
}

static void try_hook(struct game_capture *gc)
{
	if (0 && gc->config.mode == CAPTURE_MODE_ANY) {
		get_fullscreen_window(gc);
	} else {
		get_selected_window(gc);
	}

	if (gc->next_window) {
		gc->thread_id = GetWindowThreadProcessId(gc->next_window,
				&gc->process_id);

		// Make sure we never try to hook ourselves (projector)
		if (gc->process_id == GetCurrentProcessId())
			return;

		if (!gc->thread_id && gc->process_id)
			return;
		if (!gc->process_id) {
			warn("error acquiring, failed to get window "
					"thread/process ids: %lu",
					GetLastError());
			gc->error_acquiring = true;
			return;
		}

		if (!init_hook(gc)) {
			stop_capture(gc);
		}
	} else {
		gc->active = false;
	}
}

void hook(LPCTSTR windowName) 
{
	HWND hwnd = FindWindow(NULL, windowName);

	LocalOutput("%X", hwnd);
	struct game_capture *gc = game_capture_create();
	struct dstr * klass = &gc->klass;
	get_window_class(klass, hwnd);
	DebugBreak();
	try_hook(gc);
}