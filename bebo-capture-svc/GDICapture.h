#ifndef GDICAPTURE_H
#define GDICAPTURE_H
#include <tchar.h>
#include <dshow.h>
#include <windows.h>
#include <stdint.h>
class GDIFrame {
public:
	GDIFrame() : _bound(RECT()), _bitmap(), _data(nullptr) { }
	~GDIFrame() {
		DeleteObject(_bitmap);
	}

	int width() const { return _bound.right - _bound.left;  }
	int height() const { return _bound.bottom - _bound.top;  }
	int x() const { return _bound.left;  }
	int y() const { return _bound.top;  }
	int stride() const { return width() * 4; }
	uint8_t* data() const { return _data; }
	HBITMAP bitmap() { return _bitmap; }

	void updateFrame(HDC hdc, RECT bound) {
		DeleteObject(_bitmap);

		_bound = bound;

		int height = bound.bottom - bound.top;
		int width = bound.right - bound.left;
		BITMAPINFO bmi = {};
		bmi.bmiHeader.biHeight = -height;
		bmi.bmiHeader.biWidth = width;
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 4 * 8;
		bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
		bmi.bmiHeader.biSizeImage = 4 * width * height;

		HBITMAP bmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, (void**) &_data, NULL, 0);

		_bitmap = bmp;
	}

private:
	GDIFrame(RECT b, HBITMAP bmp, uint8_t* data) : _bound(b), _bitmap(bmp), _data(data) {}

	RECT _bound;
	HBITMAP _bitmap;
	uint8_t* _data;
};

class GDICapture {
public:
	GDICapture();
	~GDICapture();

	void Cleanup();
	void SetSize(int width, int height);
	void SetCaptureHandle(HWND hwnd);
	bool IsReady() { return capture_hwnd != NULL; }
	bool GetFrame(IMediaSample *pSample);
	HWND GetCaptureHandle() const { return capture_hwnd; }

private:
	int negotiated_width;
	int negotiated_height;

	bool capture_foreground;
	bool capture_screen;
	bool capture_decoration;
	bool capture_mouse;
	HWND capture_hwnd;

	BYTE* negotiated_argb_buffer;
	GDIFrame* last_frame;

	GDIFrame* CaptureFrame();
};
#endif