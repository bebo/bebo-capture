#ifndef GDICAPTURE_H
#define GDICAPTURE_H
#include <tchar.h>
#include <dshow.h>
#include <windows.h>
#include <stdint.h>
class GDICapture {
public:
	GDICapture();
	~GDICapture();

	void InitHDC(int width, int height, HWND hwnd);
	bool GetFrame(IMediaSample *pSample);
	bool IsReady() const { return initialized; }

private:
	int negotiated_width;
	int negotiated_height;
	bool initialized;

	bool capture_foreground;
	bool capture_screen;
	bool capture_decoration;
	bool capture_mouse;
	HWND capture_hwnd;

	int screen_width;
	int screen_height;
	HDC screen_hdc;
	RECT screen_rect;
	HDC mem_hdc;
	HBITMAP bitmap;
	HBITMAP old_bitmap;
	BYTE* bitmap_bits;
};
#endif