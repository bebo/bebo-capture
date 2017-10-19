#ifndef _DisplayManager_H_
#define _DisplayManager_H_

#include <tchar.h>
#include <dshow.h>
#include <windows.h>
#include <stdint.h>
#include "CommonTypes.h"

class DesktopFrame {
public:
	DesktopFrame() : _width(0), _height(0), _stride(0), _data(nullptr) {};
	DesktopFrame(int w, int h) : _width(w), _height(h), _stride(0), _data(nullptr) {};
	DesktopFrame(int width, int height, int stride, uint8_t* const data) :
		_width(width), _height(height), _stride(stride), _data(data) {};

	int width() const { return _width; };
	int height() const { return _height; };
	int stride() const { return _stride; };
	uint8_t* data() const { return _data; };

	void updateFrame(int w, int h, int stride, uint8_t* data) {
		_width = w;
		_height = h;
		_stride = stride;
		_data = data;
	}

private:
	int _width;
	int _height;
	int _stride;
	uint8_t* _data;
};

class DesktopCapture {
public:
	DesktopCapture();
	~DesktopCapture();
	void Init(int adapterId, int desktopId, int width, int height);
	
	void Cleanup();
	bool GetFrame(IMediaSample *pSimple, bool captureMouse, REFERENCE_TIME now);
	bool GetOldFrame(IMediaSample *pSimple, bool captureMouse);
	bool DoneWithFrame();
	bool IsReady() { return m_Initialized;  };

private:
	// methods
	static const int DUPLICATOR_RETRY_SECONDS = 3;

	void ProcessFrame(FrameData * Data, int OffsetX, int OffsetY);
	void CopyDirty(FrameData* Data, INT OffsetX, INT OffsetY);
	void CopyMove(FrameData* Data, INT OffsetX, INT OffsetY);
	HRESULT GetMouse(_Inout_ PtrInfo* PtrInfo, _In_ DXGI_OUTDUPL_FRAME_INFO* FrameInfo, INT OffsetX, INT OffsetY);

	HRESULT ProcessFrameMetaData(FrameData* Data);
	void SetMoveRect(_Out_ RECT* SrcRect, _Out_ RECT* DestRect, _In_ DXGI_OUTPUT_DESC* DeskDesc, _In_ DXGI_OUTDUPL_MOVE_RECT* MoveRect, INT TexWidth, INT TexHeight);

	HRESULT InitializeDXResources();
	HRESULT InitDuplication();
	HRESULT ReinitializeDuplication();

	bool AcquireNextFrame(DXGI_OUTDUPL_FRAME_INFO * frame, REFERENCE_TIME now);
	bool PushFrame(IMediaSample *pSample, DesktopFrame* frame);

	void CleanRefs();

	// variables
	DXGI_OUTPUT_DESC m_OutputDesc;	
	ID3D11Texture2D* m_StagingTexture;	
	IDXGISurface* m_Surface;
	PtrInfo* m_MouseInfo;

	ID3D11Device* m_Device;
	ID3D11DeviceContext* m_DeviceContext;
	ID3D11Texture2D* m_MoveSurf;

	int m_iAdapterNumber;
	int m_iDesktopNumber;
	bool m_Initialized;
	FrameData* m_LastFrameData;
	DesktopFrame* m_LastDesktopFrame;

	IDXGIOutputDuplication* m_DeskDupl;
	ID3D11Texture2D* m_AcquiredDesktopImage;
	BYTE* m_MetaDataBuffer;
	UINT m_MetaDataSize;

	REFERENCE_TIME m_retryTimeout;
	int m_negotiatedWidth;
	int m_negotiatedHeight;
	BYTE* m_negotiatedArgbBuffer;
};
#endif