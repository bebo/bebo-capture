#ifndef _DisplayManager_H_
#define _DisplayManager_H_

#include <tchar.h>
#include <dshow.h>
#include <windows.h>
#include <stdint.h>
#include "CommonTypes.h"

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

class DesktopCapture {
public:
	DesktopCapture();
	~DesktopCapture();
	void Init(int desktopId);
	DuplReturn ProcessFrame(_In_ FrameData* Data, _Inout_ ID3D11Texture2D* SharedSurf, INT OffsetX, INT OffsetY, _In_ DXGI_OUTPUT_DESC* DeskDesc);
	
	bool GetFrame(IMediaSample *pSimple, bool miss, int width, int height, bool captureMouse);
	bool DoneWithFrame();
	bool IsReady() { return m_Initialized;  };

	DuplReturn GetMouse(_Inout_ PtrInfo* PtrInfo, _In_ DXGI_OUTDUPL_FRAME_INFO* FrameInfo, INT OffsetX, INT OffsetY);

private:
	// methods
	DuplReturn CopyDirty(_In_ ID3D11Texture2D* SrcSurface, _Inout_ ID3D11Texture2D* SharedSurf, _In_reads_(DirtyCount) RECT* DirtyBuffer, UINT DirtyCount, INT OffsetX, INT OffsetY, _In_ DXGI_OUTPUT_DESC* DeskDesc);
	DuplReturn CopyMove(_Inout_ ID3D11Texture2D* SharedSurf, _In_reads_(MoveCount) DXGI_OUTDUPL_MOVE_RECT* MoveBuffer, UINT MoveCount, INT OffsetX, INT OffsetY, _In_ DXGI_OUTPUT_DESC* DeskDesc, INT TexWidth, INT TexHeight);
	void SetDirtyVert(_Out_writes_(NUMVERTICES) Vertex* Vertices, _In_ RECT* Dirty, INT OffsetX, INT OffsetY, _In_ DXGI_OUTPUT_DESC* DeskDesc, _In_ D3D11_TEXTURE2D_DESC* FullDesc, _In_ D3D11_TEXTURE2D_DESC* ThisDesc);
	void SetMoveRect(_Out_ RECT* SrcRect, _Out_ RECT* DestRect, _In_ DXGI_OUTPUT_DESC* DeskDesc, _In_ DXGI_OUTDUPL_MOVE_RECT* MoveRect, INT TexWidth, INT TexHeight);

	HRESULT InitializeDXResources();
	HRESULT CreateCopyBuffer();
	HRESULT CreateSharedSurf();
	HRESULT InitDupl();

	void CleanRefs();

	static bool PushFrame(IMediaSample *pSample, D3D11_TEXTURE2D_DESC frameDesc, D3D11_MAPPED_SUBRESOURCE dxgiMap,
		int width, int height);

	// variables
	DXResources* m_DXResource;
	ID3D11Texture2D* m_SharedSurf;
	ID3D11Texture2D* m_CopyBuffer;	
	DXGI_OUTPUT_DESC m_OutputDesc;	
	IDXGIKeyedMutex* m_KeyMutex;
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

	IDXGIOutputDuplication* m_DeskDupl;
	ID3D11Texture2D* m_AcquiredDesktopImage;
	BYTE* m_MetaDataBuffer;
	UINT m_MetaDataSize;
};
#endif