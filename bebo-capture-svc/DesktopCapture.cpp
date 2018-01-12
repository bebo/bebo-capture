#include "DesktopCapture.h"

using namespace DirectX;

#include "Logging.h"
#include <dshow.h>
#include <strsafe.h>
#include <tchar.h>
#include <windows.h>
#include <dxgi.h>
#include "libyuv/convert.h"
#include "libyuv/scale_argb.h"
#include "CommonTypes.h"


void error_hr(const char *message, HRESULT errorNumber) {
	const wchar_t *errorMessage;
	DWORD nLen = FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_MAX_WIDTH_MASK,
		NULL,
		errorNumber,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&errorMessage,
		0,
		NULL);

	if (nLen == 0) {
		error("%S. 0x%08x - Unknown error.", message, errorNumber);
	}else {
		error("%S. 0x%08x - %ls", message, errorNumber, errorMessage);
	}
	HeapFree(GetProcessHeap(), HEAP_NO_SERIALIZE, (LPVOID)errorMessage);
}

DesktopCapture::DesktopCapture() : m_Device(nullptr),
	m_DeviceContext(nullptr),
	m_MoveSurf(nullptr),
	m_DeskDupl(nullptr),
	m_AcquiredDesktopImage(nullptr),
	m_MetaDataBuffer(nullptr),
	m_StagingTexture(nullptr),
	m_Surface(nullptr),
	m_MetaDataSize(0),
	m_iDesktopNumber(0),
	m_iAdapterNumber(0),
	m_MouseInfo(new PtrInfo),
	m_Initialized(false),
	m_LastFrameData(new FrameData),
	m_LastDesktopFrame(new DesktopFrame),
	m_negotiatedArgbBuffer(nullptr)
{
	m_retryTimeout = 0;
	RtlZeroMemory(&m_OutputDesc, sizeof(DXGI_OUTPUT_DESC));
}

//
// Destructor calls CleanRefs to destroy everything
//
DesktopCapture::~DesktopCapture()
{
	CleanRefs();

	if (m_Device) {
		m_Device->Release();
		m_Device = nullptr;
	}

	if (m_DeviceContext) {
		m_DeviceContext->Release();
		m_DeviceContext = nullptr;
	}

	if (m_LastDesktopFrame) {
		delete m_LastDesktopFrame;
		m_LastDesktopFrame = nullptr;
	}
	
	if (m_LastFrameData) {
		delete m_LastFrameData;
		m_LastFrameData = nullptr;
	}
	
	if (m_MouseInfo) {
		delete m_MouseInfo;
		m_MouseInfo = nullptr;
	}

	if (m_negotiatedArgbBuffer) {
		delete[] m_negotiatedArgbBuffer;
		m_negotiatedArgbBuffer = nullptr;
	}
}

void DesktopCapture::CleanRefs()
{
	if (m_DeskDupl) {
		m_DeskDupl->Release();
		m_DeskDupl = nullptr;
	}

	if (m_MoveSurf) {
		m_MoveSurf->Release();
		m_MoveSurf = nullptr;
	}

	if (m_StagingTexture) {
		m_StagingTexture->Release();
		m_StagingTexture = nullptr;
	}

	if (m_Surface) {
		m_Surface->Release();
		m_Surface = nullptr;
	}

	if (m_AcquiredDesktopImage) {
		m_AcquiredDesktopImage->Release();
		m_AcquiredDesktopImage = nullptr;
	}

	if (m_MetaDataBuffer) {
		delete[] m_MetaDataBuffer;
		m_MetaDataBuffer = nullptr;
	}

	m_MetaDataSize = 0;	
}

//
// Initialize
//
void DesktopCapture::Init(int adapterId, int desktopId, int width, int height)
{
    m_iAdapterNumber = adapterId;
    m_iDesktopNumber = desktopId;
    m_negotiatedWidth = width;
    m_negotiatedHeight = height;

    if (m_negotiatedArgbBuffer) {
        delete[] m_negotiatedArgbBuffer;
    }
    m_negotiatedArgbBuffer = new BYTE[4 * m_negotiatedWidth * m_negotiatedHeight];

    HRESULT hr = InitializeDXResources();

    if (SUCCEEDED(hr)) {
        hr = InitDuplication();
    }

    m_Initialized = SUCCEEDED(hr);

    if (FAILED(hr)) {
        error_hr("Failed to initialize duplication", hr);
    }
}

HRESULT DesktopCapture::InitializeDXResources() {
	if (m_Device && m_DeviceContext) {
		debug("DX context is already initialize.");
		return S_OK;
	}

	HRESULT hr = S_OK;

	// Driver types supported
	D3D_DRIVER_TYPE DriverTypes[] =
	{
		D3D_DRIVER_TYPE_HARDWARE,
		D3D_DRIVER_TYPE_WARP,
		D3D_DRIVER_TYPE_REFERENCE,
	};
	UINT NumDriverTypes = ARRAYSIZE(DriverTypes);

	// Feature levels supported
	D3D_FEATURE_LEVEL FeatureLevels[] =
	{
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0,
		D3D_FEATURE_LEVEL_9_1
	};
	UINT NumFeatureLevels = ARRAYSIZE(FeatureLevels);

	D3D_FEATURE_LEVEL FeatureLevel;

	// Create device
	for (UINT DriverTypeIndex = 0; DriverTypeIndex < NumDriverTypes; ++DriverTypeIndex)
	{
		hr = D3D11CreateDevice(nullptr, DriverTypes[DriverTypeIndex], nullptr, 0, FeatureLevels, NumFeatureLevels,
			D3D11_SDK_VERSION, &m_Device, &FeatureLevel, &m_DeviceContext);
		if (SUCCEEDED(hr))
		{
			// Device creation success, no need to loop anymore
			break;
		}
	}

	if (FAILED(hr))
	{
		error_hr("Failed to create device in Initialize DX", hr);
		return hr;
	}

    m_Device->AddRef();
    m_DeviceContext->AddRef();
	return hr;
}

HRESULT DesktopCapture::InitDuplication() {
	HRESULT hr = S_OK;

	IDXGIDevice* DxgiDevice = nullptr;
	hr = m_Device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&DxgiDevice));
	if (FAILED(hr))
	{
		error_hr("Failed to QI for DXGI Device", hr);
		return hr;
	}

	IDXGIAdapter* DxgiAdapter = nullptr;
	hr = DxgiDevice->GetParent(__uuidof(IDXGIAdapter), reinterpret_cast<void**>(&DxgiAdapter));
	DxgiDevice->Release();
	DxgiDevice = nullptr;
	if (FAILED(hr))
	{
		error_hr("Failed to get parent DXGI Adapter", hr);
		return hr;
	}

	IDXGIFactory * DxgiFactory = nullptr;
	hr = DxgiAdapter->GetParent(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&DxgiFactory));
	DxgiAdapter->Release();
	DxgiAdapter = nullptr;
	if (FAILED(hr))
	{ 
		error_hr("Failed to get parent DXGI DxgiFactory", hr);
		return hr;
	}

	IDXGIAdapter* DxgiActualAdapter = nullptr;
	hr = DxgiFactory->EnumAdapters(m_iAdapterNumber, &DxgiActualAdapter);
	DxgiFactory->Release();
	DxgiFactory = nullptr;
	if (FAILED(hr))
	{
		error_hr("Adapter specified to be duplicated does not exist", hr);
		return hr;
	}

	IDXGIOutput* DxgiOutput = nullptr;
	// Figure out right dimensions for full size desktop texture and # of outputs to duplicate
	hr = DxgiActualAdapter->EnumOutputs(m_iDesktopNumber, &DxgiOutput);
	DxgiActualAdapter->Release();
	DxgiActualAdapter = nullptr;
	if (FAILED(hr))
	{
		error_hr("Output specified to be duplicated does not exist", hr);
		return hr;
	}

	RtlZeroMemory(&m_OutputDesc, sizeof(DXGI_OUTPUT_DESC));
	DxgiOutput->GetDesc(&m_OutputDesc);

	// QI for Output 1
	IDXGIOutput1* DxgiOutput1 = nullptr;
	hr = DxgiOutput->QueryInterface(__uuidof(DxgiOutput1), reinterpret_cast<void**>(&DxgiOutput1));
	DxgiOutput->Release();
	DxgiOutput = nullptr;
	if (FAILED(hr))
	{
		error_hr("Failed to QI for DxgiOutput1 in DesktopCapture", hr);
		return hr;
	}

	if (m_DeskDupl) {
		m_DeskDupl->Release();
		m_DeskDupl = nullptr;
	}

	// Create desktop duplication
	hr = DxgiOutput1->DuplicateOutput(m_Device, &m_DeskDupl);
	DxgiOutput1->Release();
	DxgiOutput1 = nullptr;
	if (FAILED(hr))
	{
		if (hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE)
		{
			error("There is already the maximum number of applications using the Desktop Duplication API running, please close one of those applications and then try again.");
			return hr;
		}
		error_hr("Failed to get duplicate output in DesktopCapture", hr);
		return hr;
	}

	D3D11_TEXTURE2D_DESC CopyBufferDesc;
	CopyBufferDesc.Width = m_OutputDesc.DesktopCoordinates.right - m_OutputDesc.DesktopCoordinates.left;
	CopyBufferDesc.Height = m_OutputDesc.DesktopCoordinates.bottom - m_OutputDesc.DesktopCoordinates.top;
	CopyBufferDesc.MipLevels = 1;
	CopyBufferDesc.ArraySize = 1;
	CopyBufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	CopyBufferDesc.SampleDesc.Count = 1;
	CopyBufferDesc.SampleDesc.Quality = 0;
	CopyBufferDesc.Usage = D3D11_USAGE_STAGING;
	CopyBufferDesc.BindFlags = 0;
	CopyBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	CopyBufferDesc.MiscFlags = 0;

	if (m_StagingTexture) {
		m_StagingTexture->Release();
		m_StagingTexture = nullptr;
	}

	hr = m_Device->CreateTexture2D(&CopyBufferDesc, nullptr, &m_StagingTexture);
	if (FAILED(hr))
	{
		error_hr("Failed to create staging texture", hr);
		return hr;
	}

	if (m_Surface) {
		m_Surface->Release();
		m_Surface = nullptr;
	}

	hr = m_StagingTexture->QueryInterface(__uuidof(IDXGISurface), reinterpret_cast<void**>(&m_Surface));
	if (FAILED(hr))
	{
		error_hr("Failed to QI for IDXGI staging texture surface", hr);
		return hr;
	}

	return hr;
}

//
// Process a given frame and its metadata
//
void DesktopCapture::ProcessFrame(FrameData* data, int offsetX, int offsetY)
{
	if (data->FrameInfo.TotalMetadataBufferSize) {
		// Process dirties and moves
		if (data->MoveCount) {
			CopyMove(data, offsetX, offsetY);
		}
		if (data->DirtyCount) {
			CopyDirty(data, offsetX, offsetY);
		}
	}
}

//
// Set appropriate source and destination rects for move rects
//
void DesktopCapture::SetMoveRect(_Out_ RECT* SrcRect, _Out_ RECT* DestRect, _In_ DXGI_OUTPUT_DESC* DeskDesc, _In_ DXGI_OUTDUPL_MOVE_RECT* MoveRect, INT TexWidth, INT TexHeight)
{
    switch (DeskDesc->Rotation)
    {
        case DXGI_MODE_ROTATION_UNSPECIFIED:
        case DXGI_MODE_ROTATION_IDENTITY:
        {
            SrcRect->left = MoveRect->SourcePoint.x;
            SrcRect->top = MoveRect->SourcePoint.y;
            SrcRect->right = MoveRect->SourcePoint.x + MoveRect->DestinationRect.right - MoveRect->DestinationRect.left;
            SrcRect->bottom = MoveRect->SourcePoint.y + MoveRect->DestinationRect.bottom - MoveRect->DestinationRect.top;

            *DestRect = MoveRect->DestinationRect;
            break;
        }
        case DXGI_MODE_ROTATION_ROTATE90:
        {
            SrcRect->left = TexHeight - (MoveRect->SourcePoint.y + MoveRect->DestinationRect.bottom - MoveRect->DestinationRect.top);
            SrcRect->top = MoveRect->SourcePoint.x;
            SrcRect->right = TexHeight - MoveRect->SourcePoint.y;
            SrcRect->bottom = MoveRect->SourcePoint.x + MoveRect->DestinationRect.right - MoveRect->DestinationRect.left;

            DestRect->left = TexHeight - MoveRect->DestinationRect.bottom;
            DestRect->top = MoveRect->DestinationRect.left;
            DestRect->right = TexHeight - MoveRect->DestinationRect.top;
            DestRect->bottom = MoveRect->DestinationRect.right;
            break;
        }
        case DXGI_MODE_ROTATION_ROTATE180:
        {
            SrcRect->left = TexWidth - (MoveRect->SourcePoint.x + MoveRect->DestinationRect.right - MoveRect->DestinationRect.left);
            SrcRect->top = TexHeight - (MoveRect->SourcePoint.y + MoveRect->DestinationRect.bottom - MoveRect->DestinationRect.top);
            SrcRect->right = TexWidth - MoveRect->SourcePoint.x;
            SrcRect->bottom = TexHeight - MoveRect->SourcePoint.y;

            DestRect->left = TexWidth - MoveRect->DestinationRect.right;
            DestRect->top = TexHeight - MoveRect->DestinationRect.bottom;
            DestRect->right = TexWidth - MoveRect->DestinationRect.left;
            DestRect->bottom =  TexHeight - MoveRect->DestinationRect.top;
            break;
        }
        case DXGI_MODE_ROTATION_ROTATE270:
        {
            SrcRect->left = MoveRect->SourcePoint.x;
            SrcRect->top = TexWidth - (MoveRect->SourcePoint.x + MoveRect->DestinationRect.right - MoveRect->DestinationRect.left);
            SrcRect->right = MoveRect->SourcePoint.y + MoveRect->DestinationRect.bottom - MoveRect->DestinationRect.top;
            SrcRect->bottom = TexWidth - MoveRect->SourcePoint.x;

            DestRect->left = MoveRect->DestinationRect.top;
            DestRect->top = TexWidth - MoveRect->DestinationRect.right;
            DestRect->right = MoveRect->DestinationRect.bottom;
            DestRect->bottom =  TexWidth - MoveRect->DestinationRect.left;
            break;
        }
        default:
        {
            RtlZeroMemory(DestRect, sizeof(RECT));
            RtlZeroMemory(SrcRect, sizeof(RECT));
            break;
        }
    }
}

//
// Copy move rectangles
//
void DesktopCapture::CopyMove(FrameData* data, INT offsetX, INT offsetY)
{
	DXGI_OUTDUPL_MOVE_RECT* move_buffer = reinterpret_cast<DXGI_OUTDUPL_MOVE_RECT*>(data->MetaData); 
	UINT move_count = data->MoveCount;

	D3D11_TEXTURE2D_DESC desc;
	D3D11_TEXTURE2D_DESC full_desc;

	data->Frame->GetDesc(&desc);
	m_StagingTexture->GetDesc(&full_desc);

	// Make new intermediate surface to copy into for moving
	if (!m_MoveSurf) {
		D3D11_TEXTURE2D_DESC move_desc;
		move_desc = full_desc;
		move_desc.Width = m_OutputDesc.DesktopCoordinates.right - m_OutputDesc.DesktopCoordinates.left;
		move_desc.Height = m_OutputDesc.DesktopCoordinates.bottom - m_OutputDesc.DesktopCoordinates.top;
		move_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
		move_desc.MiscFlags = 0;
		HRESULT hr = m_Device->CreateTexture2D(&move_desc, nullptr, &m_MoveSurf);
		if (FAILED(hr)) {
			error_hr("Failed to create staging texture for move rects", hr);
			return;
		}
	}

	while (move_count > 0) {
		RECT src_rect;
		RECT dest_rect;

		SetMoveRect(&src_rect, &dest_rect, &m_OutputDesc, move_buffer, desc.Width, desc.Height);

		// Copy rect out of shared surface
		D3D11_BOX box;
		box.left = src_rect.left + m_OutputDesc.DesktopCoordinates.left - offsetX;
		box.top = src_rect.top + m_OutputDesc.DesktopCoordinates.top - offsetY;
		box.front = 0;
		box.right = src_rect.right + m_OutputDesc.DesktopCoordinates.left - offsetX;
		box.bottom = src_rect.bottom + m_OutputDesc.DesktopCoordinates.top - offsetY;
		box.back = 1;
		m_DeviceContext->CopySubresourceRegion(m_MoveSurf, 0, src_rect.left, src_rect.top, 0, data->Frame, 0, &box);

		box.left = src_rect.left;
		box.top = src_rect.top;
		box.front = 0;
		box.right = src_rect.right;
		box.bottom = src_rect.bottom;
		box.back = 1;

		m_DeviceContext->CopySubresourceRegion(m_StagingTexture, 0, dest_rect.left + m_OutputDesc.DesktopCoordinates.left - offsetX, 
			dest_rect.top + m_OutputDesc.DesktopCoordinates.top - offsetY, 0, m_MoveSurf, 0, &box);

		move_buffer++;
		move_count--;
	}
}

//
// Copies dirty rectangles
//
void DesktopCapture::CopyDirty(FrameData* data, INT offsetX, INT offsetY)
{
	RECT* dirty_buffer =  reinterpret_cast<RECT*>(data->MetaData + (data->MoveCount * sizeof(DXGI_OUTDUPL_MOVE_RECT)));
	int dirty_count = data->DirtyCount;

	while (dirty_count > 0) {
		D3D11_BOX Box;
		Box.left = dirty_buffer->left + m_OutputDesc.DesktopCoordinates.left - offsetX;
		Box.top = dirty_buffer->top + m_OutputDesc.DesktopCoordinates.top - offsetY;
		Box.front = 0;
		Box.right = dirty_buffer->right + m_OutputDesc.DesktopCoordinates.left - offsetX;
		Box.bottom = dirty_buffer->bottom + m_OutputDesc.DesktopCoordinates.top - offsetY;
		Box.back = 1;

		m_DeviceContext->CopySubresourceRegion(m_StagingTexture, 0, dirty_buffer->left, dirty_buffer->top, 0, data->Frame, 0, &Box);

		dirty_buffer++;
		dirty_count--;
	}
}

static inline int getI420BufferSize(int width, int height) {
	int half_width = (width + 1) >> 1;
	int half_height = (height + 1) >> 1;
	return width * height + half_width * half_height * 2;
}

static uint8_t* copy_frame = new uint8_t[4 * 1920 * 1080];

bool DesktopCapture::PushFrame(IMediaSample* pSample, DesktopFrame* frame) {
	if (!frame->data() || frame->stride() == 0) {
		warn("push frame - no data");
		return false;
	}

	BYTE *pData;
	pSample->GetPointer(&pData);

	const uint8_t* src_frame = frame->data();
	int src_stride_frame = frame->stride();
	int src_width = frame->width();
	int src_height = frame->height();

	int scaled_argb_stride = 4 * m_negotiatedWidth;

	libyuv::ARGBScale(
		src_frame, src_stride_frame,
		src_width, src_height,
		m_negotiatedArgbBuffer, scaled_argb_stride,
		m_negotiatedWidth, m_negotiatedHeight,
		libyuv::FilterMode(libyuv::kFilterBox)
	);


	uint8* y = pData;
	int stride_y = m_negotiatedWidth;
	uint8* u = pData + (m_negotiatedWidth * m_negotiatedHeight);
	int stride_u = (m_negotiatedWidth + 1) / 2;
	uint8* v = u + ((m_negotiatedWidth * m_negotiatedHeight) >> 2);
	int stride_v = stride_u;

	libyuv::ARGBToI420(m_negotiatedArgbBuffer, scaled_argb_stride,
		y, stride_y,
		u, stride_u,
		v, stride_v,
		m_negotiatedWidth, m_negotiatedHeight);

	return true;
}

//
// Retrieves mouse info and write it into PtrInfo
//
HRESULT DesktopCapture::GetMouse(_Inout_ PtrInfo* PtrInfo, _In_ DXGI_OUTDUPL_FRAME_INFO* FrameInfo, INT OffsetX, INT OffsetY)
{
	// A non-zero mouse update timestamp indicates that there is a mouse position update and optionally a shape change
	if (FrameInfo->LastMouseUpdateTime.QuadPart == 0)
	{
		return S_OK;
	}

	bool UpdatePosition = true;

	// Make sure we don't update pointer position wrongly
	// If pointer is invisible, make sure we did not get an update from another output that the last time that said pointer
	// was visible, if so, don't set it to invisible or update.
	if (!FrameInfo->PointerPosition.Visible && (PtrInfo->WhoUpdatedPositionLast != m_iDesktopNumber))
	{
		UpdatePosition = false;
	}

	// If two outputs both say they have a visible, only update if new update has newer timestamp
	if (FrameInfo->PointerPosition.Visible && PtrInfo->Visible && (PtrInfo->WhoUpdatedPositionLast != m_iDesktopNumber) && (PtrInfo->LastTimeStamp.QuadPart > FrameInfo->LastMouseUpdateTime.QuadPart))
	{
		UpdatePosition = false;
	}

	// Update position
	if (UpdatePosition)
	{
		PtrInfo->Position.x = FrameInfo->PointerPosition.Position.x + m_OutputDesc.DesktopCoordinates.left - OffsetX;
		PtrInfo->Position.y = FrameInfo->PointerPosition.Position.y + m_OutputDesc.DesktopCoordinates.top - OffsetY;
		PtrInfo->WhoUpdatedPositionLast = m_iDesktopNumber;
		PtrInfo->LastTimeStamp = FrameInfo->LastMouseUpdateTime;
		PtrInfo->Visible = FrameInfo->PointerPosition.Visible != 0;
	}

	// No new shape
	if (FrameInfo->PointerShapeBufferSize == 0)
	{
		return S_OK;
	}

	// Old buffer too small
	if (FrameInfo->PointerShapeBufferSize > PtrInfo->BufferSize)
	{
		if (PtrInfo->PtrShapeBuffer)
		{
			delete[] PtrInfo->PtrShapeBuffer;
			PtrInfo->PtrShapeBuffer = nullptr;
		}
		PtrInfo->PtrShapeBuffer = new (std::nothrow) BYTE[FrameInfo->PointerShapeBufferSize];
		if (!PtrInfo->PtrShapeBuffer)
		{
			PtrInfo->BufferSize = 0;
			error("Failed to allocate memory for pointer shape in DesktopCapture");
			return E_UNEXPECTED;
		}

		// Update buffer size
		PtrInfo->BufferSize = FrameInfo->PointerShapeBufferSize;
	}

	// Get shape
	UINT BufferSizeRequired;
	HRESULT hr = m_DeskDupl->GetFramePointerShape(FrameInfo->PointerShapeBufferSize, reinterpret_cast<VOID*>(PtrInfo->PtrShapeBuffer), &BufferSizeRequired, &(PtrInfo->ShapeInfo));
	if (FAILED(hr))
	{
		delete[] PtrInfo->PtrShapeBuffer;
		PtrInfo->PtrShapeBuffer = nullptr;
		PtrInfo->BufferSize = 0;
		error_hr("Failed to get frame pointer shape in DesktopCapture", hr);
		return E_UNEXPECTED;
	}

	return S_OK;
}


bool DesktopCapture::AcquireNextFrame(DXGI_OUTDUPL_FRAME_INFO * frame, REFERENCE_TIME now) {
	HRESULT hr = S_OK;
	IDXGIResource* desktop_resource = nullptr;

	if (!m_DeskDupl) {
		hr = E_FAIL;
		if (m_retryTimeout == 0) {
			m_retryTimeout = now + 10000000L * DUPLICATOR_RETRY_SECONDS;
		}
		if (now >= m_retryTimeout) {
			m_retryTimeout = now + 10000000L * DUPLICATOR_RETRY_SECONDS;
			hr = ReinitializeDuplication();
		}
	}

	if (FAILED(hr)) {
		return false;
	}

	hr = m_DeskDupl->AcquireNextFrame(0, frame, &desktop_resource);
	if (hr == DXGI_ERROR_ACCESS_LOST) {
		error("Failed to acquire next frame - dxgi error access lost.");
		CleanRefs(); // clean it up so it gets recreate later
		return false;
	} else if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
		// debug("Failed to acquire next frame - timeout.");
		return false;
	} else if (FAILED(hr)) {
		error_hr("Failed to acquire next frame in DesktopCapture", hr);
		return false;
	}

	// If still holding old frame, destroy it
	if (m_AcquiredDesktopImage) {
		m_AcquiredDesktopImage->Release();
		m_AcquiredDesktopImage = nullptr;
	}

	// QI for IDXGIResource
	hr = desktop_resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&m_AcquiredDesktopImage));
	desktop_resource->Release();
	desktop_resource = nullptr;

	if (FAILED(hr)) {
		error_hr("Failed to QI for ID3D11Texture2D from acquired IDXGIResource in DesktopCapture", hr);
		DoneWithFrame();
		return false;
	}

	return SUCCEEDED(hr);
}

HRESULT DesktopCapture::ProcessFrameMetaData(FrameData* Data) {
	HRESULT Ret = S_OK;

	// process meta data
	if (Data->FrameInfo.TotalMetadataBufferSize) {
		// Old buffer too small
		if (Data->FrameInfo.TotalMetadataBufferSize > m_MetaDataSize) {
			if (m_MetaDataBuffer) {
				delete[] m_MetaDataBuffer;
				m_MetaDataBuffer = nullptr;
			}

			m_MetaDataBuffer = new (std::nothrow) BYTE[Data->FrameInfo.TotalMetadataBufferSize];

			if (!m_MetaDataBuffer) {
				m_MetaDataSize = 0;
				Data->MoveCount = 0;
				Data->DirtyCount = 0;
				error("Failed to allocate memory for meta data");
				return E_UNEXPECTED;
			}
			m_MetaDataSize = Data->FrameInfo.TotalMetadataBufferSize;
		}

		UINT BufSize = Data->FrameInfo.TotalMetadataBufferSize;
		// Get move rectangles
		Ret = m_DeskDupl->GetFrameMoveRects(BufSize, reinterpret_cast<DXGI_OUTDUPL_MOVE_RECT*>(m_MetaDataBuffer), &BufSize);
		if (FAILED(Ret)) {
			Data->MoveCount = 0;
			Data->DirtyCount = 0;
			error("Failed to frame move rects");
			return Ret;
		}

		Data->MoveCount = BufSize / sizeof(DXGI_OUTDUPL_MOVE_RECT);
		BYTE* DirtyRects = m_MetaDataBuffer + BufSize;

		BufSize = Data->FrameInfo.TotalMetadataBufferSize - BufSize;

		// Get dirty rectangles

		Ret = m_DeskDupl->GetFrameDirtyRects(BufSize, reinterpret_cast<RECT*>(DirtyRects), &BufSize);
		if (FAILED(Ret)) {
			Data->MoveCount = 0;
			Data->DirtyCount = 0;
			error("Failed to dirty frame rects");
			return Ret;
		}
		Data->DirtyCount = BufSize / sizeof(RECT);
		Data->MetaData = m_MetaDataBuffer;
	}

	return Ret;
}

void DesktopCapture::Cleanup() 
{
	CleanRefs();
	m_Initialized = false;
}

//
// Get next frame and write it into Data
//
bool DesktopCapture::GetFrame(IMediaSample *pSample, bool captureMouse, REFERENCE_TIME now)
{
	if (!m_Initialized) {
		error("DesktopCapture.Init() required.");
		return false;
	}

	DXGI_OUTDUPL_FRAME_INFO frame_info = { 0 };
	DXGI_SURFACE_DESC frame_desc = { 0 };
	DXGI_MAPPED_RECT map;

	bool got_frame = AcquireNextFrame(&frame_info, now);

	if (!got_frame) {
		return false;
	}

	m_LastFrameData->Frame = m_AcquiredDesktopImage;
	m_LastFrameData->FrameInfo = frame_info;

	int offset_x = m_OutputDesc.DesktopCoordinates.left;
	int offset_y = m_OutputDesc.DesktopCoordinates.top;
	
	ProcessFrameMetaData(m_LastFrameData);
	ProcessFrame(m_LastFrameData, offset_x, offset_y);

	m_Surface->GetDesc(&frame_desc);

	m_Surface->Map(&map, D3D11_MAP_READ);
    m_LastDesktopFrame->updateFrame(frame_desc.Width, frame_desc.Height, map.Pitch, map.pBits);
    got_frame = PushFrame(pSample, m_LastDesktopFrame);
	m_Surface->Unmap();

	DoneWithFrame();

	return got_frame;
}

bool DesktopCapture::GetOldFrame(IMediaSample *pSample, bool captureMouse)
{
	if (!m_LastDesktopFrame) {
		error("last desktop frame required.");
		return false;
	}

	return PushFrame(pSample, m_LastDesktopFrame);
}

//
// Release frame
//
bool DesktopCapture::DoneWithFrame()
{
	if (!m_DeskDupl) {
		return false;
	}
	HRESULT hr = m_DeskDupl->ReleaseFrame();
	 
	if (FAILED(hr)) {
		error_hr("Failed to release frame", hr);
	}

	if (hr == DXGI_ERROR_ACCESS_LOST) {
		CleanRefs(); // clean it up so it gets recreate later
	}

	if (m_AcquiredDesktopImage) {
		m_AcquiredDesktopImage->Release();
		m_AcquiredDesktopImage = nullptr;
	}

	return SUCCEEDED(hr);
}

HRESULT DesktopCapture::ReinitializeDuplication() {
	CleanRefs();

	HRESULT hr = InitDuplication();
	if (FAILED(hr)) {
		CleanRefs();
		error("Failed to init desktop duplication in reinitialization");
		return hr;
	}
	m_retryTimeout = 0;
	info("Reinitializing duplication successful");
	return hr;
}
