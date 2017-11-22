#include "GameCapture.h"

#include <chrono>
#include "Logging.h"
#include <dshow.h>
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
#include "libyuv/convert.h"
#include "libyuv/scale.h"
#include "CommonTypes.h"
#include "registry.h"

#define STOP_BEING_BAD \
	    "This is most likely due to security software" \
        "that the Bebo Capture installation folder is excluded/ignored in the " \
        "settings of the security software you are using."

extern "C" {
	static std::vector<std::string> logged_file;
	char *bebo_find_file(const char *file_c) {
		RegKey machine_registry(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Bebo\\GameCapture", KEY_READ);

		const std::wstring key(L"Directory");
		HRESULT r;
		std::string file(file_c);
		std::wstring out;

		if (machine_registry.HasValue(key.c_str())) {
			machine_registry.ReadValue(key.c_str(), &out);
		} else {
			warn("can not find registry key in HKLM, fallback to HKCU: %ls", key.c_str());
			RegKey local_registry(HKEY_CURRENT_USER, L"Software\\Bebo\\GameCapture", KEY_READ);
			if (!local_registry.HasValue(key.c_str())) {
				return NULL;
			}
			local_registry.ReadValue(key.c_str(), &out);
		}


		const wchar_t* out_c = out.c_str();
		CHAR * result = (CHAR *)bmalloc(wcslen(out_c) + strlen(file_c) + 1);
		wsprintfA(result, "%S\\%s", out_c, file_c);

		bool found_in_vector = false;
		for (auto &it : logged_file) {
			if (it.compare(file) == 0) {
				found_in_vector = true;
				break;
			}
		}

		if (!found_in_vector) {
			info("RegGetBeboSZ %ls, %ls, %S", key.c_str(), out_c, result);
			logged_file.push_back(file);
		}

		return result;
	}
	struct graphics_offsets offsets32 = { 0 };
	struct graphics_offsets offsets64 = { 0 };
}

enum capture_mode {
	CAPTURE_MODE_ANY,
	CAPTURE_MODE_WINDOW,
	CAPTURE_MODE_HOTKEY
};

static uint32_t inject_failed_count = 0;

struct game_capture {
	int                           last_tex;

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
	volatile long                 hotkey_window;
	volatile bool                 deactivate_hook;
	volatile bool                 activate_hook_now;
	LONG64						  frame_interval;
	bool                          wait_for_target_startup;
	bool                          showing;
	bool                          active;
	bool                          capturing;
	bool                          activate_hook;
	bool                          process_is_64bit;
	bool                          error_acquiring;
	bool                          dwm_capture;
	bool                          initial_config;
	bool                          convert_16bit;
	bool                          is_app;

	struct game_capture_config    config;

	ipc_pipe_server_t             pipe;
	struct hook_info              *global_hook_info;
	HANDLE                        keepalive_mutex;
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

	bool (*copy_texture)(struct game_capture*, IMediaSample *pSample);
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

	return gc->is_app
		? open_app_map(gc->app_sid, new_name)
		: OpenFileMappingW(GC_MAPPING_FLAGS, false, new_name);
}

static inline HANDLE open_hook_info(struct game_capture *gc)
{
	return open_map_plus_id(gc, SHMEM_HOOK_INFO, gc->process_id);
}
static struct game_capture *game_capture_create(game_capture_config *config, uint64_t frame_interval)
{
	struct game_capture *gc = (struct game_capture*) bzalloc(sizeof(*gc));

	gc->config.priority = config->priority;
	gc->config.mode = config->mode;
	gc->config.scale_cx = config->scale_cx;
	gc->config.scale_cy = config->scale_cy;
	gc->config.cursor = config->cursor;
	gc->config.force_shmem = config->force_shmem;
	gc->config.force_scaling = config->force_scaling;
	gc->config.allow_transparency = config->allow_transparency;
	gc->config.limit_framerate = config->limit_framerate;
	gc->config.capture_overlays = config->capture_overlays;
	gc->config.anticheat_hook = inject_failed_count > 10 ? true : config->anticheat_hook;
	gc->frame_interval = frame_interval;
	gc->last_tex = -1;

	gc->initial_config = true;
	gc->priority = config->priority;
	gc->wait_for_target_startup = false;
	gc->window = config->window;

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
		warn("hook_direct: could not open process: %S (%lu) %S, %S",
				gc->config.executable, GetLastError(), gc->config.title, gc->config.klass);
		return false;
	}

	ret = inject_library(process, hook_path_abs_w);
	CloseHandle(process);

	if (ret != 0) {
		error("hook_direct: inject failed: %d, anti_cheat: %d, %S, %S, %S", ret, gc->config.anticheat_hook, gc->config.title, gc->config.klass, gc->config.executable);
		if (ret == INJECT_ERROR_UNLIKELY_FAIL) {
			inject_failed_count++;
		}
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
	"chrome",
	"firefox",
	"systemsettings",
	"applicationframehost",
	"cmd",
	"bebo",
	"epicgameslauncher",
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

		if (_strcmpi(cur_exe, exe) == 0)
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
			PROCESS_QUERY_INFORMATION | SYNCHRONIZE,
			false, gc->process_id);
	if (!gc->target_process) {
		warn("could not open process: %S (%lu) %S, %S",
				gc->config.executable, GetLastError(), gc->config.title, gc->config.klass);
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
		warn("Game capture %S not found.", STOP_BEING_BAD, name);
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
		warn("Game capture file '%S' not found."
				STOP_BEING_BAD, file);
	} else if (error == ERROR_ACCESS_DENIED) {
		warn("Game capture file '%S' could not be loaded."
				STOP_BEING_BAD, file);
	} else {
		warn("Game capture file '%S' could not be loaded: %lu."
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
		warn("Failed to create inject helper process: %S (%lu)",
			gc->config.executable, GetLastError());
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

	info("injecting %S with %S into %S", hook_dll, inject_path, gc->config.executable);

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

		if (!success && inject_failed_count > 10) {
			gc->config.anticheat_hook = true;
			info("hook_direct: inject failed for 10th time, retrying with helper (%S hook)", use_anticheat(gc) ?
				"compatibility" : "direct");
			success = create_inject_process(gc, inject_path, hook_dll);
		}
	} else {
		info("using helper (%S hook)", use_anticheat(gc) ?
				"compatibility" : "direct");
		success = create_inject_process(gc, inject_path, hook_dll);
	}

	if (success) {
		inject_failed_count = 0;
	}

cleanup:
	bfree(inject_path);
	bfree(hook_path);
	return success;
}

static inline bool init_keepalive(struct game_capture *gc)
{
	wchar_t new_name[64];
	_snwprintf(new_name, 64, L"%ls%lu", WINDOW_HOOK_KEEPALIVE,
		gc->process_id);

	gc->keepalive_mutex = CreateMutexW(NULL, false, new_name);
	if (!gc->keepalive_mutex) {
		warn("Failed to create keepalive mutex: %lu", GetLastError());
		return false;
	}
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
		info("%S", data);
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
	gc->global_hook_info->frame_interval = gc->frame_interval;
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

	gc->global_hook_info->offsets = gc->process_is_64bit ?  offsets64 : offsets32;
	gc->global_hook_info->capture_overlay = gc->config.capture_overlays;
	gc->global_hook_info->force_shmem = true;
	gc->global_hook_info->use_scale = gc->config.force_scaling;
	gc->global_hook_info->cx = gc->config.scale_cx;
	gc->global_hook_info->cy = gc->config.scale_cy;
	reset_frame_interval(gc);

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
	gc->hook_stop = open_event_gc(gc, EVENT_CAPTURE_STOP);
	if (gc->hook_stop) {
		info("existing hook found, signaling process: %S",
			gc->config.executable);
		SetEvent(gc->hook_stop);
		return true;
	}

	return false;
}

static bool init_hook(struct game_capture *gc)
{
	struct dstr exe = {0};
	bool blacklisted_process = false;

	if (0 && gc->config.mode == CAPTURE_MODE_ANY) {
		if (get_window_exe(&exe, gc->next_window)) {
			info("attempting to hook fullscreen process: %S", exe.array);
		}
	}
	else {
		info("attempting to hook process: %S", gc->executable.array);
		dstr_copy_dstr(&exe, &gc->executable);
	}

	blacklisted_process = is_blacklisted_exe(exe.array);
	if (blacklisted_process)
		info("cannot capture %S due to being blacklisted", exe.array);
	dstr_free(&exe);

	if (blacklisted_process) {
		return false;
	}
	if (target_suspended(gc)) {
		info("target is suspended");
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

static void stop_capture(struct game_capture *gc)
{
	ipc_pipe_server_free(&gc->pipe);

	if (gc->hook_stop) {
		SetEvent(gc->hook_stop);
	}

	if (gc->global_hook_info) {
		UnmapViewOfFile(gc->global_hook_info);
		gc->global_hook_info = NULL;
	}

	if (gc->data) {
		UnmapViewOfFile(gc->data);
		gc->data = NULL;
	}

	if (gc->app_sid) {
		LocalFree(gc->app_sid);
		gc->app_sid = NULL;
	}

	close_handle(&gc->hook_restart);
	close_handle(&gc->hook_stop);
	close_handle(&gc->hook_ready);
	close_handle(&gc->hook_exit);
	close_handle(&gc->hook_init);
	close_handle(&gc->hook_data_map);
	close_handle(&gc->keepalive_mutex);
	close_handle(&gc->global_hook_info_map);
	close_handle(&gc->target_process);
	close_handle(&gc->texture_mutexes[0]);
	close_handle(&gc->texture_mutexes[1]);

	if (gc->active)
		info("game capture stopped");

	gc->copy_texture = NULL;
	gc->wait_for_target_startup = false;
	gc->active = false;
	gc->capturing = false;

	if (gc->retrying)
		gc->retrying--;
}

HWND dbg_last_window = NULL;
static void try_hook(struct game_capture *gc)
{
	if (0 && gc->config.mode == CAPTURE_MODE_ANY) {
		get_fullscreen_window(gc);
	} else {
		get_selected_window(gc);
	}

	if (gc->next_window) {
		if (gc->next_window != dbg_last_window) {
			info("hooking next window: %X, %S, %S", gc->next_window, gc->config.title, gc->config.klass);
			dbg_last_window = gc->next_window;
		}
		gc->thread_id = GetWindowThreadProcessId(gc->next_window, &gc->process_id);

		// Make sure we never try to hook ourselves (projector)
		if (gc->process_id == GetCurrentProcessId())
			return;

		if (!gc->thread_id && gc->process_id)
			return;

		if (!gc->process_id) {
			warn("error acquiring, failed to get window thread/process ids: %lu",
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

bool isReady(void ** data) {
	if (*data == NULL) {
		return false;
	}
	struct game_capture *gc = (game_capture *) *data;
//	debug("isReady - data active: %d && retrying %d - %d", gc->active, gc->retrying, gc->active && gc->capturing);
	return gc->active && ! gc->retrying;
}

void set_fps(void **data, uint64_t frame_interval) {
	struct game_capture *gc = (game_capture *) *data;

	if (gc == NULL) {
		debug("set_fps: gc==NULL");
		return;
	}
	debug("set_fps: %d", frame_interval);
	gc->global_hook_info->frame_interval = frame_interval;
}

void * hook(void **data, LPCWSTR windowClassName, LPCWSTR windowName, game_capture_config *config, uint64_t frame_interval)
{
	struct game_capture *gc = (game_capture *) *data;
	if (gc == NULL) {
		HWND hwnd = NULL;
		window_priority priority = WINDOW_PRIORITY_EXE;

		if (windowClassName != NULL && lstrlenW(windowClassName) > 0 && 
			windowName != NULL && lstrlenW(windowName) > 0) {
			hwnd = FindWindowW(windowClassName, windowName);
		}

		if (hwnd == NULL &&
			windowClassName != NULL && lstrlenW(windowClassName) > 0) {
			hwnd = FindWindowW(windowClassName, NULL);
			priority = WINDOW_PRIORITY_CLASS;
		}

		if (hwnd == NULL &&
			windowName != NULL && lstrlenW(windowName) > 0) {
			hwnd = FindWindowW(NULL, windowName);
			priority = WINDOW_PRIORITY_TITLE;
		}

		if (hwnd == NULL) {
			return NULL;
		}

		config->window = hwnd;

		gc = game_capture_create(config, frame_interval);

		struct dstr *klass = &gc->klass;
		struct dstr *title = &gc->title;
		struct dstr *exe = &gc->executable;
		get_window_class(klass, hwnd);
		get_window_exe(exe, hwnd);
		get_window_title(title, hwnd);

		gc->config.executable = _strdup(exe->array);
		gc->config.title = _strdup(title->array);
		gc->config.klass = _strdup(klass->array);;

		gc->priority = priority;
	}

	try_hook(gc);
	if (gc->active || gc->retrying) {
		return gc;
	}

	return NULL;
}

enum capture_result {
	CAPTURE_FAIL,
	CAPTURE_RETRY,
	CAPTURE_SUCCESS
};

static inline enum capture_result init_capture_data(struct game_capture *gc)
{
	gc->cx = gc->global_hook_info->cx;
	gc->cy = gc->global_hook_info->cy;
	gc->pitch = gc->global_hook_info->pitch;

	if (gc->data) {
		UnmapViewOfFile(gc->data);
		gc->data = NULL;
	}

	CloseHandle(gc->hook_data_map);

	gc->hook_data_map = open_map_plus_id(gc, SHMEM_TEXTURE,
			gc->global_hook_info->map_id);
	if (!gc->hook_data_map) {
		DWORD error = GetLastError();
		if (error == 2) {
			return CAPTURE_RETRY;
		} else {
			warn("init_capture_data: failed to open file "
			     "mapping: %lu", error);
		}
		return CAPTURE_FAIL;
	}

	gc->data = MapViewOfFile(gc->hook_data_map, FILE_MAP_ALL_ACCESS, 0, 0,
			gc->global_hook_info->map_size);
	if (!gc->data) {
		warn("init_capture_data: failed to map data view: %lu",
				GetLastError());
		return CAPTURE_FAIL;
	}

	info("init_capture_data successful for %S, %S, %S", gc->config.title, gc->config.klass, gc->config.executable);
	return CAPTURE_SUCCESS;
}

static inline bool is_16bit_format(uint32_t format)
{
	return format == DXGI_FORMAT_B5G5R5A1_UNORM ||
	       format == DXGI_FORMAT_B5G6R5_UNORM;
}

static void copy_b5g6r5_tex(struct game_capture *gc, int cur_texture,
		uint8_t *data, uint32_t pitch)
{
	uint8_t *input = gc->texture_buffers[cur_texture];
	uint32_t gc_cx = gc->cx;
	uint32_t gc_cy = gc->cy;
	uint32_t gc_pitch = gc->pitch;

	for (uint32_t y = 0; y < gc_cy; y++) {
		uint8_t *row = input + (gc_pitch * y);
		uint8_t *out = data + (pitch * y);

		for (uint32_t x = 0; x < gc_cx; x += 8) {
			__m128i pixels_blue, pixels_green, pixels_red;
			__m128i pixels_result;
			__m128i *pixels_dest;

			__m128i *pixels_src = (__m128i*)(row + x * sizeof(uint16_t));
			__m128i pixels = _mm_load_si128(pixels_src);

			__m128i zero = _mm_setzero_si128();
			__m128i pixels_low = _mm_unpacklo_epi16(pixels, zero);
			__m128i pixels_high = _mm_unpackhi_epi16(pixels, zero);

			__m128i blue_channel_mask = _mm_set1_epi32(0x0000001F);
			__m128i blue_offset = _mm_set1_epi32(0x00000003);
			__m128i green_channel_mask = _mm_set1_epi32(0x000007E0);
			__m128i green_offset = _mm_set1_epi32(0x00000008);
			__m128i red_channel_mask = _mm_set1_epi32(0x0000F800);
			__m128i red_offset = _mm_set1_epi32(0x00000300);

			pixels_blue = _mm_and_si128(pixels_low, blue_channel_mask);
			pixels_blue = _mm_slli_epi32(pixels_blue, 3);
			pixels_blue = _mm_add_epi32(pixels_blue, blue_offset);

			pixels_green = _mm_and_si128(pixels_low, green_channel_mask);
			pixels_green = _mm_add_epi32(pixels_green, green_offset);
			pixels_green = _mm_slli_epi32(pixels_green, 5);

			pixels_red = _mm_and_si128(pixels_low, red_channel_mask);
			pixels_red = _mm_add_epi32(pixels_red, red_offset);
			pixels_red = _mm_slli_epi32(pixels_red, 8);

			pixels_result = _mm_set1_epi32(0xFF000000);
			pixels_result = _mm_or_si128(pixels_result, pixels_blue);
			pixels_result = _mm_or_si128(pixels_result, pixels_green);
			pixels_result = _mm_or_si128(pixels_result, pixels_red);

			pixels_dest = (__m128i*)(out + x * sizeof(uint32_t));
			_mm_store_si128(pixels_dest, pixels_result);

			pixels_blue = _mm_and_si128(pixels_high, blue_channel_mask);
			pixels_blue = _mm_slli_epi32(pixels_blue, 3);
			pixels_blue = _mm_add_epi32(pixels_blue, blue_offset);

			pixels_green = _mm_and_si128(pixels_high, green_channel_mask);
			pixels_green = _mm_add_epi32(pixels_green, green_offset);
			pixels_green = _mm_slli_epi32(pixels_green, 5);

			pixels_red = _mm_and_si128(pixels_high, red_channel_mask);
			pixels_red = _mm_add_epi32(pixels_red, red_offset);
			pixels_red = _mm_slli_epi32(pixels_red, 8);

			pixels_result = _mm_set1_epi32(0xFF000000);
			pixels_result = _mm_or_si128(pixels_result, pixels_blue);
			pixels_result = _mm_or_si128(pixels_result, pixels_green);
			pixels_result = _mm_or_si128(pixels_result, pixels_red);

			pixels_dest = (__m128i*)(out + (x + 4) * sizeof(uint32_t));
			_mm_store_si128(pixels_dest, pixels_result);
		}
	}
}

static void copy_b5g5r5a1_tex(struct game_capture *gc, int cur_texture,
		uint8_t *data, uint32_t pitch)
{
	uint8_t *input = gc->texture_buffers[cur_texture];
	uint32_t gc_cx = gc->cx;
	uint32_t gc_cy = gc->cy;
	uint32_t gc_pitch = gc->pitch;

	for (uint32_t y = 0; y < gc_cy; y++) {
		uint8_t *row = input + (gc_pitch * y);
		uint8_t *out = data + (pitch * y);

		for (uint32_t x = 0; x < gc_cx; x += 8) {
			__m128i pixels_blue, pixels_green, pixels_red, pixels_alpha;
			__m128i pixels_result;
			__m128i *pixels_dest;

			__m128i *pixels_src = (__m128i*)(row + x * sizeof(uint16_t));
			__m128i pixels = _mm_load_si128(pixels_src);

			__m128i zero = _mm_setzero_si128();
			__m128i pixels_low = _mm_unpacklo_epi16(pixels, zero);
			__m128i pixels_high = _mm_unpackhi_epi16(pixels, zero);

			__m128i blue_channel_mask = _mm_set1_epi32(0x0000001F);
			__m128i blue_offset = _mm_set1_epi32(0x00000003);
			__m128i green_channel_mask = _mm_set1_epi32(0x000003E0);
			__m128i green_offset = _mm_set1_epi32(0x000000C);
			__m128i red_channel_mask = _mm_set1_epi32(0x00007C00);
			__m128i red_offset = _mm_set1_epi32(0x00000180);
			__m128i alpha_channel_mask = _mm_set1_epi32(0x00008000);
			__m128i alpha_offset = _mm_set1_epi32(0x00000001);
			__m128i alpha_mask32 = _mm_set1_epi32(0xFF000000);

			pixels_blue = _mm_and_si128(pixels_low, blue_channel_mask);
			pixels_blue = _mm_slli_epi32(pixels_blue, 3);
			pixels_blue = _mm_add_epi32(pixels_blue, blue_offset);

			pixels_green = _mm_and_si128(pixels_low, green_channel_mask);
			pixels_green = _mm_add_epi32(pixels_green, green_offset);
			pixels_green = _mm_slli_epi32(pixels_green, 6);

			pixels_red = _mm_and_si128(pixels_low, red_channel_mask);
			pixels_red = _mm_add_epi32(pixels_red, red_offset);
			pixels_red = _mm_slli_epi32(pixels_red, 9);

			pixels_alpha = _mm_and_si128(pixels_low, alpha_channel_mask);
			pixels_alpha = _mm_srli_epi32(pixels_alpha, 15);
			pixels_alpha = _mm_sub_epi32(pixels_alpha, alpha_offset);
			pixels_alpha = _mm_andnot_si128(pixels_alpha, alpha_mask32);

			pixels_result = pixels_red;
			pixels_result = _mm_or_si128(pixels_result, pixels_alpha);
			pixels_result = _mm_or_si128(pixels_result, pixels_blue);
			pixels_result = _mm_or_si128(pixels_result, pixels_green);

			pixels_dest = (__m128i*)(out + x * sizeof(uint32_t));
			_mm_store_si128(pixels_dest, pixels_result);

			pixels_blue = _mm_and_si128(pixels_high, blue_channel_mask);
			pixels_blue = _mm_slli_epi32(pixels_blue, 3);
			pixels_blue = _mm_add_epi32(pixels_blue, blue_offset);

			pixels_green = _mm_and_si128(pixels_high, green_channel_mask);
			pixels_green = _mm_add_epi32(pixels_green, green_offset);
			pixels_green = _mm_slli_epi32(pixels_green, 6);

			pixels_red = _mm_and_si128(pixels_high, red_channel_mask);
			pixels_red = _mm_add_epi32(pixels_red, red_offset);
			pixels_red = _mm_slli_epi32(pixels_red, 9);

			pixels_alpha = _mm_and_si128(pixels_high, alpha_channel_mask);
			pixels_alpha = _mm_srli_epi32(pixels_alpha, 15);
			pixels_alpha = _mm_sub_epi32(pixels_alpha, alpha_offset);
			pixels_alpha = _mm_andnot_si128(pixels_alpha, alpha_mask32);

			pixels_result = pixels_red;
			pixels_result = _mm_or_si128(pixels_result, pixels_alpha);
			pixels_result = _mm_or_si128(pixels_result, pixels_blue);
			pixels_result = _mm_or_si128(pixels_result, pixels_green);

			pixels_dest = (__m128i*)(out + (x + 4) * sizeof(uint32_t));
			_mm_store_si128(pixels_dest, pixels_result);
		}
	}
}

static inline void copy_16bit_tex(struct game_capture *gc, int cur_texture,
		uint8_t *data, uint32_t pitch)
{
	if (gc->global_hook_info->format == DXGI_FORMAT_B5G5R5A1_UNORM) {
		copy_b5g5r5a1_tex(gc, cur_texture, data, pitch);

	} else if (gc->global_hook_info->format == DXGI_FORMAT_B5G6R5_UNORM) {
		copy_b5g6r5_tex(gc, cur_texture, data, pitch);
	}
}

static bool copy_shmem_tex(struct game_capture *gc, IMediaSample *pSample)
{
	int cur_texture = gc->shmem_data->last_tex;

	if (cur_texture == gc->last_tex) {
		debug("last_tex didn't change - try again later");
		return false;
	}

	HANDLE mutex = NULL;
	uint32_t pitch;
	int next_texture;

	if (cur_texture < 0 || cur_texture > 1)
		return false;

	next_texture = cur_texture == 1 ? 0 : 1;

	debug("FRAME - %d", cur_texture);
	if (object_signalled(gc->texture_mutexes[cur_texture])) {
		mutex = gc->texture_mutexes[cur_texture];
	} else if (object_signalled(gc->texture_mutexes[next_texture])) {
		debug("FRAME B - %d", next_texture);
		mutex = gc->texture_mutexes[next_texture];
		cur_texture = next_texture;
	} else {
		warn("NO FRAME - try again");
		return false;
	}

	gc->last_tex = cur_texture;

	BYTE *pData;
    pSample->GetPointer(&pData);

	pitch = gc->pitch;

	if (gc->convert_16bit) {

		/// FIXME LOG ERROR
		warn("copy_shmem_text 16 bit - not handled");
		copy_16bit_tex(gc, cur_texture, pData, pitch);

	} else if (pitch == gc->pitch) {

		// Convert camera sample to I420 with cropping, rotation and vertical flip.
		// "src_size" is needed to parse MJPG.
		// "dst_stride_y" number of bytes in a row of the dst_y plane.
		//   Normally this would be the same as dst_width, with recommended alignment
		//   to 16 bytes for better efficiency.
		//   If rotation of 90 or 270 is used, stride is affected. The caller should
		//   allocate the I420 buffer according to rotation.
		// "dst_stride_u" number of bytes in a row of the dst_u plane.
		//   Normally this would be the same as (dst_width + 1) / 2, with
		//   recommended alignment to 16 bytes for better efficiency.
		//   If rotation of 90 or 270 is used, stride is affected.
		// "crop_x" and "crop_y" are starting position for cropping.
		//   To center, crop_x = (src_width - dst_width) / 2
		//              crop_y = (src_height - dst_height) / 2
		// "src_width" / "src_height" is size of src_frame in pixels.
		//   "src_height" can be negative indicating a vertically flipped image source.
		// "crop_width" / "crop_height" is the size to crop the src to.
		//    Must be less than or equal to src_width/src_height
		//    Cropping parameters are pre-rotation.
		// "rotation" can be 0, 90, 180 or 270.
		// "format" is a fourcc. ie 'I420', 'YUY2'
		// Returns 0 for successful; -1 for invalid parameter. Non-zero for failure.


		// FIXME make sure 16 byte alignment!
		const uint8* src_frame = gc->texture_buffers[cur_texture];
		int src_stride_frame = pitch;

		int width = gc->cx;
		int height = gc->cy;
		uint8* dst_y = pData;
		int dst_stride_y = width;
		uint8* dst_u = pData + (width * height);
		int dst_stride_u = (width + 1) / 2;
		//uint8* dst_v = pData + ((width * height) >> 2);
		uint8* dst_v = dst_u + ((width * height) >> 2);
		int dst_stride_v = dst_stride_u;

		if (gc->global_hook_info->flip) {
			height = -height;
		}

		// TODO - better to initialize function pointer once ?
		int err = NULL;
		if (gc->global_hook_info->format == DXGI_FORMAT_R8G8B8A8_UNORM) {
			// Overwatch
			err = libyuv::ABGRToI420(src_frame,
				src_stride_frame,
				dst_y,
				dst_stride_y,
				dst_u,
				dst_stride_u,
				dst_v,
				dst_stride_v,
				width,
				height);
		} else if (gc->global_hook_info->format == DXGI_FORMAT_B8G8R8A8_UNORM) {
			// Hearthstone
			// opengl / minecraft (javaw.exe)
			err = libyuv::ARGBToI420(src_frame,
				src_stride_frame,
				dst_y,
				dst_stride_y,
				dst_u,
				dst_stride_u,
				dst_v,
				dst_stride_v,
				width,
				height);
		} else if (gc->global_hook_info->format == DXGI_FORMAT_B8G8R8X8_UNORM) {
			// League Of Legends 7.2.17
			err = libyuv::ARGBToI420(src_frame,
				src_stride_frame,
				dst_y,
				dst_stride_y,
				dst_u,
				dst_stride_u,
				dst_v,
				dst_stride_v,
				width,
				height);
		} else if (gc->global_hook_info->format == DXGI_FORMAT_R10G10B10A2_UNORM) {
			// unreal engine, pubg
			err = ABGR10ToI420(src_frame,
				src_stride_frame,
				dst_y,
				dst_stride_y,
				dst_u,
				dst_stride_u,
				dst_v,
				dst_stride_v,
				width,
				height);
				
		} else {
			warn("Unknown DXGI FORMAT %d", gc->global_hook_info->format);
		}

		if (err) {
			warn("yuv conversion failed");
		}

	} else {
		error("Unexpected state - no pitch");
		uint8_t *input = gc->texture_buffers[cur_texture];
		uint32_t best_pitch =
			pitch < gc->pitch ? pitch : gc->pitch;

		for (uint32_t y = 0; y < gc->cy; y++) {

			uint8_t *line_in = input + gc->pitch * y;
			uint8_t *line_out = pData + pitch * y;
			memcpy(line_out, line_in, best_pitch);
		}
	}

	ReleaseMutex(mutex);

	return true;
}

static inline bool init_shmem_capture(struct game_capture *gc)
{
	gc->texture_buffers[0] = (uint8_t*)gc->data + gc->shmem_data->tex1_offset;
	gc->texture_buffers[1] = (uint8_t*)gc->data + gc->shmem_data->tex2_offset;
	gc->convert_16bit = is_16bit_format(gc->global_hook_info->format);
	gc->copy_texture = copy_shmem_tex;
	return true;
}

static inline bool init_shtex_capture(struct game_capture *gc)
{
	return false;
}

static bool start_capture(struct game_capture *gc)
{
	if (gc->global_hook_info->type == CAPTURE_TYPE_MEMORY) {
		if (!init_shmem_capture(gc)) {
			return false;
		}

		info("memory capture successful for %S, %S, %S", gc->config.title,  gc->config.klass, gc->config.executable);
	} else {
		if (!init_shtex_capture(gc)) {
			return false;
		}

		info("shared texture capture successful");
	}

	return true;
}

static inline bool capture_valid(struct game_capture *gc)
{
	if (!gc->dwm_capture && !IsWindow(gc->window))
	       return false;
	
	return !object_signalled(gc->target_process);
}

bool get_game_frame(void **data, bool missed, IMediaSample *pSample) {
	/*
	 * Direct Show and OBS have a different strategy on dealing with frames
	 * 
	 * For Direct Show / webrtc we really only want to deliver new frames,
	 * if we loose a frame, better to loose it here than send (and encode) a duplicate
	 * on a machine that is probably already overloaded
	 * 
	 * There are a couple of rules here:
	 * 
	 * We don't want to capture higher than the target fps
	 *  -> we set the target fps in the graphics hook to keep impact to the graphics pipeline low
	 * OBS graphics-hook has no semaphore on "new texture" available,
	 * the only thing we can trigger off is the last_tex id, which is 0 or 1
	 *
	 * So we sample at 2 times fps, if last_text_id changes -> we have a new sample
	 * If the mutex is locked - we use the other sample
	 * 
	 * If we are late we need to get both frames and check 
	 * If we are late
	 * We don't want to return frames d
	*/


	struct game_capture *gc = (game_capture *) *data;
	if (!gc->active) {
		*data = NULL;
		return false;
	}

	if (missed) {
		gc->last_tex = -1;
	}

	// TODO there are more interesting cases handled in the obs game_capture_tick - need to re-asses those

	if (gc->active && !gc->hook_ready && gc->process_id) {
		debug("re-subscribing to hook_ready");
		gc->hook_ready = open_event_gc(gc, EVENT_HOOK_READY);
	}

	if (gc->hook_ready && object_signalled(gc->hook_ready)) {
		debug("capture initializing!");
		gc->copy_texture = NULL;  // Don't copy textures if hook has been freed
		enum capture_result result = init_capture_data(gc);

		if (result == CAPTURE_SUCCESS)
			gc->capturing = start_capture(gc);
		else
			info("init_capture_data failed");

// FIXME: this is odd that we commented this out:
//		if (result != CAPTURE_RETRY && !gc->capturing) {
//			gc->retry_interval = ERROR_RETRY_INTERVAL;
//			stop_capture(gc);
//		}
	}

	//gc->retry_time += seconds;

	if (gc->active) {

		if (!capture_valid(gc)) {
			info("capture window no longer exists, "
			     "terminating capture");
			stop_capture(gc);
		} else {
			if (gc->shmem_data == NULL) {
				return false;
			}
			if (gc->copy_texture) {
				return gc->copy_texture(gc, pSample);
			}
		}
//
//			if (gc->config.cursor) {
//				obs_enter_graphics();
//				cursor_capture(&gc->cursor_data);
//				obs_leave_graphics();
//			}
//
//			gc->fps_reset_time += seconds;
//			if (gc->fps_reset_time >= gc->retry_interval) {
//				reset_frame_interval(gc);
//				gc->fps_reset_time = 0.0f;
//			}
//		}
	}

//	if (!gc->showing)
//		gc->showing = true;
	return false;
}

bool stop_game_capture(void **data) {
	struct game_capture *gc = (game_capture *) *data;
	stop_capture(gc);
	return true;
}


// color conversion
static __inline int RGBToY(uint8_t r, uint8_t g, uint8_t b) {
	return (66 * r + 129 * g + 25 * b + 0x1080) >> 8;
}

static __inline int RGBToU(uint8_t r, uint8_t g, uint8_t b) {
	return (112 * b - 74 * g - 38 * r + 0x8080) >> 8;
}
static __inline int RGBToV(uint8_t r, uint8_t g, uint8_t b) {
	return (112 * r - 94 * g - 18 * b + 0x8080) >> 8;
}

void ABGR10ToYRow_C(const uint8_t* src_argb0, uint8_t* dst_y, int width) {
	int x;
	for (x = 0; x < width; ++x) {
		uint8_t r = (src_argb0[0] & 0xFC) >> 2 | (src_argb0[1] & 0x3) << 6;
		uint8_t g = (src_argb0[1] & 0xF0) >> 4 | (src_argb0[2] & 0xF) << 4;
		uint8_t b = (src_argb0[2] & 0xC0) >> 6 | (src_argb0[3] & 0x3F) << 2;
		dst_y[0] = RGBToY(r, g, b);
		src_argb0 += 4;
		dst_y += 1;
	}
}


void ABGR10ToUVRow_C(const uint8_t* src_rgb0, int src_stride_rgb,
	uint8_t* dst_u, uint8_t* dst_v, int width) {
	const uint8_t* src_rgb1 = src_rgb0 + src_stride_rgb;
	int x;
	for (x = 0; x < width - 1; x += 2) {
		uint8_t r0 = (src_rgb0[0] & 0xFC) >> 2 | (src_rgb0[1] & 0x3) << 6;
		uint8_t g0 = (src_rgb0[1] & 0xF0) >> 4 | (src_rgb0[2] & 0xF) << 4;
		uint8_t b0 = (src_rgb0[2] & 0xC0) >> 6 | (src_rgb0[3] & 0x3F) << 2;

		uint8_t r1 = (src_rgb0[4] & 0xFC) >> 2 | (src_rgb0[5] & 0x3) << 6;
		uint8_t g1 = (src_rgb0[5] & 0xF0) >> 4 | (src_rgb0[6] & 0xF) << 4;
		uint8_t b1 = (src_rgb0[6] & 0xC0) >> 6 | (src_rgb0[7] & 0x3F) << 2;

		uint8_t r2 = (src_rgb1[0] & 0xFC) >> 2 | (src_rgb1[1] & 0x3) << 6;
		uint8_t g2 = (src_rgb1[1] & 0xF0) >> 4 | (src_rgb1[2] & 0xF) << 4;
		uint8_t b2 = (src_rgb1[2] & 0xC0) >> 6 | (src_rgb1[3] & 0x3F) << 2;

		uint8_t r3 = (src_rgb1[4] & 0xFC) >> 2 | (src_rgb1[5] & 0x3) << 6;
		uint8_t g3 = (src_rgb1[5] & 0xF0) >> 4 | (src_rgb1[6] & 0xF) << 4;
		uint8_t b3 = (src_rgb1[6] & 0xC0) >> 6 | (src_rgb1[7] & 0x3F) << 2;

		uint8_t ab = (b0 + b1 + b2 + b3) >> 2;
		uint8_t ag = (g0 + g1 + g2 + g3) >> 2;
		uint8_t ar = (r0 + r1 + r2 + r3) >> 2;

		dst_u[0] = RGBToU(ar, ag, ab);
		dst_v[0] = RGBToV(ar, ag, ab);
		src_rgb0 += 4 * 2;
		src_rgb1 += 4 * 2;
		dst_u += 1;
		dst_v += 1;
	}
	if (width & 1) {
		uint8_t r0 = (src_rgb0[0] & 0xFC) >> 2 | (src_rgb0[1] & 0x3) << 6;
		uint8_t g0 = (src_rgb0[1] & 0xF0) >> 4 | (src_rgb0[2] & 0xF) << 4;
		uint8_t b0 = (src_rgb0[2] & 0xC0) >> 6 | (src_rgb0[3] & 0x3F) << 2;

		uint8_t r2 = (src_rgb1[0] & 0xFC) >> 2 | (src_rgb1[1] & 0x3) << 6;
		uint8_t g2 = (src_rgb1[1] & 0xF0) >> 4 | (src_rgb1[2] & 0xF) << 4;
		uint8_t b2 = (src_rgb1[2] & 0xC0) >> 6 | (src_rgb1[3] & 0x3F) << 2;

		uint8_t ab = (b0 + b2) >> 1;
		uint8_t ag = (g0 + g2) >> 1;
		uint8_t ar = (r0 + r2) >> 1;

		dst_u[0] = RGBToU(ar, ag, ab);
		dst_v[0] = RGBToV(ar, ag, ab);
	}
}

int ABGR10ToI420(const uint8_t* src_argb,
	int src_stride_argb,
	uint8_t* dst_y,
	int dst_stride_y,
	uint8_t* dst_u,
	int dst_stride_u,
	uint8_t* dst_v,
	int dst_stride_v,
	int width,
	int height) {
	int y;
	void(*ARGBToUVRow)(const uint8_t* src_argb0, int src_stride_argb, uint8_t* dst_u,
		uint8_t* dst_v, int width) = ABGR10ToUVRow_C;
	void(*ARGBToYRow)(const uint8_t* src_argb, uint8_t* dst_y, int width) = ABGR10ToYRow_C;

	if (!src_argb || !dst_y || !dst_u || !dst_v || width <= 0 || height == 0) {
		return -1;
	}

	// Negative height means invert the image.
	if (height < 0) {
		height = -height;
		src_argb = src_argb + (height - 1) * src_stride_argb;
		src_stride_argb = -src_stride_argb;
	}

	for (y = 0; y < height - 1; y += 2) {
		ARGBToUVRow(src_argb, src_stride_argb, dst_u, dst_v, width);
		ARGBToYRow(src_argb, dst_y, width);
		ARGBToYRow(src_argb + src_stride_argb, dst_y + dst_stride_y, width);
		src_argb += src_stride_argb * 2;
		dst_y += dst_stride_y * 2;
		dst_u += dst_stride_u;
		dst_v += dst_stride_v;
	}

	if (height & 1) {
		ARGBToUVRow(src_argb, 0, dst_u, dst_v, width);
		ARGBToYRow(src_argb, dst_y, width);
	}
	return 0;
}


