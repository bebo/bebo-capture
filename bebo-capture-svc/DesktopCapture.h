#ifndef _DisplayManager_H_
#define _DisplayManager_H_

#include <tchar.h>
#include <dshow.h>
#include <windows.h>
#include <stdint.h>
#include "CommonTypes.h"
#include <mutex>

struct desktop_capture_config {
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

class Rect {
public:
	Rect() : _left(0), _top(0), _right(0), _bottom(0) {}

	static Rect Make(int width, int height) {	
		return Rect(0, 0, width, height);
	}

	int left() { return _left;  }
	int top() { return _top;  }
	int right() { return _right;  }
	int bottom() { return _bottom;  }
	int width() { return _right - _left;  }
	int height() { return _bottom - _top;  }

	bool is_empty() const { return _left >= _right || _top >= _bottom; }
	bool equals(const Rect& other) const {
		return _left == other._left && _top == other._top &&
			_right == other._right && _bottom == other._bottom;
	}

private:
	Rect(int left, int top, int right, int bottom): _left(left), _top(top), _right(right), _bottom(bottom) {}

	int _left;
	int _top;
	int _right;
	int _bottom;
};

class DesktopCapture {
public:
	DesktopCapture();
	~DesktopCapture();
	void Init(int desktopId);
	
	bool GetFrame(IMediaSample *pSimple, bool miss, int width, int height, bool captureMouse);
	bool DoneWithFrame();
	bool IsReady() { return m_Initialized;  };


private:
	// methods
	HRESULT CopyDirty(_In_ ID3D11Texture2D* SrcSurface, _Inout_ ID3D11Texture2D* SharedSurf, _In_reads_(DirtyCount) RECT* DirtyBuffer, UINT DirtyCount, INT OffsetX, INT OffsetY, _In_ DXGI_OUTPUT_DESC* DeskDesc);
	HRESULT CopyMove(_Inout_ ID3D11Texture2D* SharedSurf, _In_reads_(MoveCount) DXGI_OUTDUPL_MOVE_RECT* MoveBuffer, UINT MoveCount, INT OffsetX, INT OffsetY, _In_ DXGI_OUTPUT_DESC* DeskDesc, INT TexWidth, INT TexHeight);
	HRESULT ProcessFrame(_In_ FrameData* Data, _Inout_ ID3D11Texture2D* SharedSurf, INT OffsetX, INT OffsetY, _In_ DXGI_OUTPUT_DESC* DeskDesc);
	HRESULT GetMouse(_Inout_ PtrInfo* PtrInfo, _In_ DXGI_OUTDUPL_FRAME_INFO* FrameInfo, INT OffsetX, INT OffsetY);
	void SetDirtyVert(_Out_writes_(NUMVERTICES) Vertex* Vertices, _In_ RECT* Dirty, INT OffsetX, INT OffsetY, _In_ DXGI_OUTPUT_DESC* DeskDesc, _In_ D3D11_TEXTURE2D_DESC* FullDesc, _In_ D3D11_TEXTURE2D_DESC* ThisDesc);
	void SetMoveRect(_Out_ RECT* SrcRect, _Out_ RECT* DestRect, _In_ DXGI_OUTPUT_DESC* DeskDesc, _In_ DXGI_OUTDUPL_MOVE_RECT* MoveRect, INT TexWidth, INT TexHeight);

	HRESULT InitializeDXResources();
	HRESULT CreateSurface();
	HRESULT InitDupl();
	HRESULT ReinitializeDuplication();

	bool AcquireNextFrame(DXGI_OUTDUPL_FRAME_INFO * frame, IDXGIResource ** resource);

	void CleanRefs();

	static bool PushFrame(IMediaSample *pSample, DXGI_SURFACE_DESC frameDesc, DXGI_MAPPED_RECT dxgiMap,
		int width, int height);

	// variables
	DXGI_OUTPUT_DESC m_OutputDesc;	
	DXResources* m_DXResource;
	ID3D11Texture2D* m_CopyBuffer;	
	IDXGISurface* m_Surface;
	PtrInfo* m_MouseInfo;

	RECT* m_DesktopBounds;
	ID3D11Device* m_Device;
	ID3D11DeviceContext* m_DeviceContext;
	ID3D11Texture2D* m_MoveSurf;
	ID3D11VertexShader* m_VertexShader;
	ID3D11PixelShader* m_PixelShader;
	ID3D11InputLayout* m_InputLayout;
	ID3D11RenderTargetView* m_RTV;
	ID3D11SamplerState* m_SamplerLinear;
	BYTE* m_DirtyVertexBufferAlloc;
	UINT m_DirtyVertexBufferAllocSize;
	int m_iDesktopNumber;
	bool m_Initialized;
	FrameData* m_LastFrameData;

	std::mutex m_Mutex;
	Rect m_SurfaceRect;

	IDXGIOutputDuplication* m_DeskDupl;
	ID3D11Texture2D* m_AcquiredDesktopImage;
	BYTE* m_MetaDataBuffer;
	UINT m_MetaDataSize;
};
#endif