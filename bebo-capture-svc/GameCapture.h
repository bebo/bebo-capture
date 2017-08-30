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

int ABGR10ToI420(const uint8_t* src_argb, int src_stride_argb, uint8_t* dst_y, int dst_stride_y, uint8_t* dst_u,int dst_stride_u, uint8_t* dst_v, int dst_stride_v, int width, int height);
void ABGR10ToYRow_C(const uint8_t* src_argb0, uint8_t* dst_y, int width);
void ABGR10ToUVRow_C(const uint8_t* src_rgb0, int src_stride_rgb, uint8_t* dst_u, uint8_t* dst_v, int width);
