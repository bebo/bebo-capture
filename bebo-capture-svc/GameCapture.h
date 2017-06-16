#pragma once

#include <tchar.h>
#include <dshow.h>
#include <windows.h>
#include <stdint.h>
#include "CommonTypes.h"

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

boolean isReady(void ** data);
void * hook(void **data, LPCWSTR windowClassName, LPCWSTR windowName, game_capture_config *config, uint64_t frame_interval);
boolean get_game_frame(void ** data, boolean missed, IMediaSample *pSample);
boolean stop_game_capture(void ** data);
void set_fps(void **data, uint64_t frame_interval);