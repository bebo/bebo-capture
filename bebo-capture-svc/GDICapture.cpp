#include "GDICapture.h"

#include <comdef.h>
#include <dshow.h>
#include <strsafe.h>
#include <tchar.h>
#include <windows.h>
#include <wmsdkidl.h>
#include <dxgi.h>
#include <thread>
#include "DibHelper.h"
#include "window-helpers.h"
#include "Logging.h"
#include "libyuv/convert.h"
#include "libyuv/scale_argb.h"

GDICapture::GDICapture():
	negotiated_width(0),
	negotiated_height(0),
	capture_foreground(false),
	capture_screen(false),
	capture_decoration(false),
	capture_mouse(false),
	capture_hwnd(false),
	last_frame(new GDIFrame),
	negotiated_argb_buffer(nullptr)
{
}

GDICapture::~GDICapture() {
	if (last_frame) {
		delete last_frame;
	}

	if (negotiated_argb_buffer) {
		delete[] negotiated_argb_buffer;
	}
}

void GDICapture::SetSize(int width, int height) {
	negotiated_width = width;
	negotiated_height = height;

	if (negotiated_argb_buffer) {
		delete[] negotiated_argb_buffer;
	}

	negotiated_argb_buffer = new BYTE[4 * negotiated_width * negotiated_height];
}

void GDICapture::SetCaptureHandle(HWND handle) {
	capture_hwnd = handle;

	if (handle == NULL && last_frame) {
		delete last_frame;
		last_frame = new GDIFrame;
	}
}

GDIFrame* GDICapture::CaptureFrame()
{
	if (!capture_hwnd) {
		return NULL;
	}

	if (!IsWindow(capture_hwnd)) {
		return NULL;
	}

	if ((IsIconic(capture_hwnd) || !IsWindowVisible(capture_hwnd)) && last_frame) {
		return last_frame;
	}

	HDC hdc_target = GetDC(capture_hwnd);

	RECT capture_window_rect;
	GetClientRect(capture_hwnd, &capture_window_rect);

	last_frame->updateFrame(hdc_target, capture_window_rect);

	GDIFrame* frame = last_frame;

	HDC mem_hdc = CreateCompatibleDC(hdc_target);

	HGDIOBJ old_bmp = SelectObject(mem_hdc, frame->bitmap());
	if (!old_bmp || old_bmp == HGDI_ERROR) {
		error("SelectObject failed from mem_hdc into bitmap.");
		return NULL;
	}

	int cx = GetSystemMetrics(SM_CXSIZEFRAME);
	int cy = GetSystemMetrics(SM_CYSIZEFRAME);
	BitBlt(mem_hdc, 0, 0, 
		frame->width(), frame->height(), 
		hdc_target, 
		cx, cy, SRCCOPY);

	SelectObject(mem_hdc, old_bmp);
	ReleaseDC(capture_hwnd, mem_hdc);
	DeleteDC(mem_hdc);
	ReleaseDC(capture_hwnd, hdc_target);

	return frame;
}

bool GDICapture::GetFrame(IMediaSample *pSample)
{
	GDIFrame* frame = CaptureFrame();
	if (frame == NULL) {
		return false;
	}

	BYTE *pdata;
	pSample->GetPointer(&pdata);

	const uint8_t* src_frame = frame->data();
	int src_stride_frame = frame->stride();
	int src_width = frame->width();
	int src_height = frame->height();

	int scaled_argb_stride = 4 * negotiated_width;

	libyuv::ARGBScale(
		src_frame, src_stride_frame,
		src_width, src_height,
		negotiated_argb_buffer, scaled_argb_stride,
		negotiated_width, negotiated_height,
		libyuv::FilterMode(libyuv::kFilterBox)
	);

	uint8* y = pdata;
	int stride_y = negotiated_width;
	uint8* u = pdata + (negotiated_width * negotiated_height);
	int stride_u = (negotiated_width + 1) / 2;
	uint8* v = u + ((negotiated_width * negotiated_height) >> 2);
	int stride_v = stride_u;

	libyuv::ARGBToI420(negotiated_argb_buffer, scaled_argb_stride,
		y, stride_y,
		u, stride_u,
		v, stride_v,
		negotiated_width, negotiated_height);

	return true;
}

