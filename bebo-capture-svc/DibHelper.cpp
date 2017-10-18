//------------------------------------------------------------------------------
// File: DibHelper.cpp
//
// Desc: DirectShow sample code - In-memory push mode source filter
//       Helper routines for manipulating bitmaps.
//
// Copyright (c) Microsoft Corporation.  All rights reserved.
//------------------------------------------------------------------------------

#include <windows.h>

#include "dibhelper.h"
#include "Logging.h"

#include <stdio.h>
#include <assert.h>
// dwm turn off shtuff
// #pragma comment(lib,"dwmapi.lib")  // ?
#include <dwmapi.h>

extern int show_performance;

long double PCFreqMillis = 0.0;

// this call only needed once...
// who knows if this is useful or not, speed-wise...
void WarmupCounter()
{
	LARGE_INTEGER li;
	BOOL ret = QueryPerformanceFrequency(&li);
	ASSERT_RAISE(ret != 0); // only gets run in debug mode LODO
	PCFreqMillis = (long double(li.QuadPart)) / 1000.0;
}

__int64 StartCounter() // costs 0.0041 ms to do a "start and get" of these...pretty cheap
{
	LARGE_INTEGER li;
	QueryPerformanceCounter(&li);
	return (__int64)li.QuadPart;
}

long double GetCounterSinceStartMillis(__int64 sinceThisTime) // see above for some timing
{
	LARGE_INTEGER li;
	QueryPerformanceCounter(&li);
	ASSERT_RAISE(PCFreqMillis != 0.0); // make sure it's been initialized...this never happens
	return long double(li.QuadPart - sinceThisTime) / PCFreqMillis; //division kind of forces us to return a double of some sort...
} // LODO do I really need long double here? no really.

void AddMouse(HDC hMemDC, LPRECT lpRect, HDC hScrDC, HWND hwnd) {
	__int64 start = StartCounter();
	POINT p;

	// GetCursorPos(&p); // get current x, y 0.008 ms

	CURSORINFO globalCursor;
	globalCursor.cbSize = sizeof(CURSORINFO); // could cache I guess...
	::GetCursorInfo(&globalCursor);
	HCURSOR hcur = globalCursor.hCursor;

	GetCursorPos(&p);
	if (hwnd)
		ScreenToClient(hwnd, &p); // 0.010ms

	ICONINFO iconinfo;
	BOOL ret = ::GetIconInfo(hcur, &iconinfo); // 0.09ms

	if (ret) {
		p.x -= iconinfo.xHotspot; // align mouse, I guess...
		p.y -= iconinfo.yHotspot;

		// avoid some memory leak or other...
		if (iconinfo.hbmMask) {
			::DeleteObject(iconinfo.hbmMask);
		}
		if (iconinfo.hbmColor) {
			::DeleteObject(iconinfo.hbmColor);
		}
	}

	DrawIcon(hMemDC, p.x - lpRect->left, p.y - lpRect->top, hcur); // 0.042ms
	if (show_performance)
		debug("add mouse took %.02f ms", GetCounterSinceStartMillis(start)); // sum takes around 0.125 ms
}
