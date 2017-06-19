#include "GDICapture.h"

#include <comdef.h>
#include <dshow.h>
#include <strsafe.h>
#include <tchar.h>
#include <windows.h>
#include <wmsdkidl.h>
#include <dxgi.h>
#include "DibHelper.h"
#include "window-helpers.h"
#include "Logging.h"
#include "libyuv/convert.h"
#include "libyuv/scale.h"

GDICapture::GDICapture():
	negotiated_width(0),
	negotiated_height(0),
	initialized(false),
	capture_foreground(false),
	capture_screen(false),
	capture_decoration(false),
	capture_mouse(false),
	capture_hwnd(false),
	screen_hdc(nullptr),
	mem_hdc(nullptr),
	bitmap(0),
	old_bitmap(0)
{

}

GDICapture::~GDICapture() {
	ReleaseDC(NULL, mem_hdc);
	DeleteDC(mem_hdc);
	ReleaseDC(NULL, screen_hdc);
	DeleteDC(screen_hdc);
}

void GDICapture::InitHDC(int width, int height, HWND hwnd) {
	initialized = true;
	negotiated_width = width;
	negotiated_height = height;
	capture_hwnd = hwnd;
	capture_decoration = true;

	if (capture_hwnd) {
		if (capture_decoration) {
			screen_hdc = GetWindowDC(capture_hwnd);
		} else {
			screen_hdc = GetDC(capture_hwnd);
		}
	} else if (capture_screen) {
		//CreateDC(TEXT("DISPLAY"), NULL, NULL, NULL);
		screen_hdc = GetDC(NULL);
	} else if (capture_foreground) {
		screen_hdc = GetDC(GetForegroundWindow());
	}

	if (screen_hdc == 0) {
		throw "HDC not found";
	}

	// Get the dimensions of the main desktop window as the default
	GetWindowRect(capture_hwnd, &screen_rect);

	mem_hdc = CreateCompatibleDC(screen_hdc);

	int screen_width = screen_rect.right - screen_rect.left;
	int screen_height = screen_rect.bottom - screen_rect.top;
	BITMAPINFO bi = { 0 };
	BITMAPINFOHEADER *bih = &bi.bmiHeader;
	bih->biSize = sizeof(BITMAPINFOHEADER);
	bih->biBitCount = 32;
	bih->biWidth = screen_width;
	bih->biHeight = screen_height * -1;
	bih->biPlanes = 1;
	bitmap_bits = new BYTE[4 * screen_width * screen_height];
	bitmap = CreateDIBSection(mem_hdc, &bi,
		DIB_RGB_COLORS, (void**)&bitmap_bits,
		NULL, 0);
}

static inline int getI420BufferSize(int width, int height) {
	int half_width = (width + 1) >> 1;
	int half_height = (height + 1) >> 1;
	return width * height + half_width * half_height * 2;
}

bool GDICapture::GetFrame(IMediaSample *pSample)
{
	debug("CopyScreenToDataBlock - start");
	BYTE *pdata;
	pSample->GetPointer(&pdata);

	int width = (screen_rect.right - screen_rect.left);
	int height = (screen_rect.bottom - screen_rect.top);

	// determine points of where to grab from it, though I think we control these with m_rScreen
	int screen_x = screen_rect.left;
	int screen_y = screen_rect.top;

	debug("screen rect size: %dx%d, xy: %dx%d", width, height, screen_x, screen_y);

	// select new bitmap into memory DC
	SelectObject(mem_hdc, bitmap);

	// if (m_bCaptureMouse)
	//	AddMouse(hMemDC, &m_rScreen, hScrDC, m_iHwndToTrack);

		// copy it to a temporary buffer first
	// doDIBits(screen_hdc, bitmap, negotiated_height, old_frame_buffer, &tweakableHeader);
	BitBlt(mem_hdc, 0, 0, width, height, screen_hdc, screen_x, screen_y, SRCCOPY);

	BYTE* yuv = new BYTE[getI420BufferSize(width, height)];

	uint8* y = yuv;
	int stride_y = width;
	uint8* u = yuv + (width * height);
	int stride_u = (width + 1) / 2;
	uint8* v = u + ((width * height) >> 2);
	int stride_v = stride_u;

	libyuv::ARGBToI420(bitmap_bits, width * 4,
		y, stride_y,
		u, stride_u,
		v, stride_v,
		width, height);

	int dst_width = negotiated_width;
	int dst_height = negotiated_height;
	uint8* dst_y = pdata;
	int dst_stride_y = dst_width;
	uint8* dst_u = pdata + (dst_width * dst_height);
	int dst_stride_u = (dst_width + 1) / 2;
	uint8* dst_v = dst_u + ((dst_width * dst_height) >> 2);
	int dst_stride_v = dst_stride_u;

	libyuv::I420Scale(
		y, stride_y,
		u, stride_u,
		v, stride_v,
		width, height,
		dst_y, dst_stride_y,
		dst_u, dst_stride_u,
		dst_v, dst_stride_v,
		dst_width, dst_height,
		libyuv::FilterMode(libyuv::kFilterBox)
	);

	delete[] yuv;
	return true;
}