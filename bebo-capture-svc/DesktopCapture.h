#ifndef _DisplayManager_H_
#define _DisplayManager_H_

#include <tchar.h>
#include <dshow.h>
#include <windows.h>
#include <stdint.h>
#include "CommonTypes.h"

class DesktopFrame {
public:
	DesktopFrame(int width, int height, int stride, uint8_t* const data) :
		_width(width), _height(height), _stride(stride), _data(data) {};

	int width() const { return _width; };
	int height() const { return _height; };
	int stride() const { return _stride; };
	uint8_t* data() const { return _data; };

private:
	const int _stride;
	const int _width;
	const int _height;
	uint8_t* const _data;
};

class DesktopCapture {
public:
	DesktopCapture();
	~DesktopCapture();
	void Init(int adapterId, int desktopId);
	
	bool GetFrame(IMediaSample *pSimple, int width, int height, bool captureMouse);
	bool GetOldFrame(IMediaSample *pSimple, int width, int height, bool captureMouse);
	bool DoneWithFrame();
	bool IsReady() { return m_Initialized;  };
	bool ExceedMaxRetry() { return m_InitializeFailCount >= MAX_INIT_RETRY_COUNT;  };

private:
	// methods
	static const int MAX_INIT_RETRY_COUNT = 20;

	void ProcessFrame(FrameData * Data, int OffsetX, int OffsetY);
	void CopyDirty(FrameData* Data, INT OffsetX, INT OffsetY);
	void CopyMove(FrameData* Data, INT OffsetX, INT OffsetY);
	HRESULT GetMouse(_Inout_ PtrInfo* PtrInfo, _In_ DXGI_OUTDUPL_FRAME_INFO* FrameInfo, INT OffsetX, INT OffsetY);

	HRESULT ProcessFrameMetaData(FrameData* Data);
	void SetMoveRect(_Out_ RECT* SrcRect, _Out_ RECT* DestRect, _In_ DXGI_OUTPUT_DESC* DeskDesc, _In_ DXGI_OUTDUPL_MOVE_RECT* MoveRect, INT TexWidth, INT TexHeight);

	HRESULT InitializeDXResources();
	HRESULT InitDuplication();
	HRESULT ReinitializeDuplication();

	bool AcquireNextFrame(DXGI_OUTDUPL_FRAME_INFO * frame);
	bool PushFrame(IMediaSample *pSample, DesktopFrame* frame, int width, int height);

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
	int m_InitializeFailCount;
	FrameData* m_LastFrameData;
	DesktopFrame* m_LastDesktopFrame;

	IDXGIOutputDuplication* m_DeskDupl;
	ID3D11Texture2D* m_AcquiredDesktopImage;
	BYTE* m_MetaDataBuffer;
	UINT m_MetaDataSize;
};
#endif