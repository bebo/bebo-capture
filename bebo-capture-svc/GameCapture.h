#pragma once

#include <tchar.h>
#include <dshow.h>
#include <windows.h>
void * hook(LPCWSTR windowName);
boolean get_game_frame(void ** data, float seconds, IMediaSample *pSample);
boolean stop_game_capture(void ** data);
