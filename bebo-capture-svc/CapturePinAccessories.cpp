#include <streams.h>

#include "Capture.h"
#include "CaptureGuids.h"
#include "DibHelper.h"
#include "Logging.h"

#include <dvdmedia.h>
#include <wmsdkidl.h>

const int PIN_RESOLUTION_SIZE = 12;
const int PIN_FPS_SIZE = 1;
const int PIN_WIDTH[PIN_RESOLUTION_SIZE] = { 640, 854, 1120, // 4:3, 16:9, 21:9
					960, 1280, 1680,
					1200, 1600, 2100,
					1440, 1920, 2560 };
const int PIN_HEIGHT[PIN_RESOLUTION_SIZE] = { 480, 480, 480,
					720, 720, 720,
					900, 900, 900,
					1080, 1080, 1080 };
const REFERENCE_TIME PIN_FPS[PIN_FPS_SIZE] = { UNITS / 60 };

// logging stuff
int DisplayRECT(wchar_t *buffer, size_t count, const RECT& rc)
{
	return _snwprintf(buffer, count, L"%dx%d[%d:%d]",
		rc.right - rc.left,
		rc.top - rc.bottom,
		rc.right,
		rc.bottom);
}

int DisplayBITMAPINFO(wchar_t *buffer, size_t count, const BITMAPINFOHEADER* pbmi)
{
	if (pbmi->biCompression < 256) {
		return _snwprintf(buffer, count, L"[bitmap: %dx%dx%d bit  (%d)] size:%d (%d/%d)",
			pbmi->biWidth,
			pbmi->biHeight,
			pbmi->biBitCount,
			pbmi->biCompression,
			pbmi->biSizeImage,
			pbmi->biPlanes,
			pbmi->biClrUsed);
	}
	else {
		// TOOD cant test the biCompression oddity and compiler complains
		//return snprintf(buffer, count, "[bitmap: %dx%dx%d bit '%4.4hx' size:%d (%d/%d)",
		return _snwprintf(buffer, count, L"[bitmap: %dx%dx%d bit '?' size:%d (%d/%d)",
			pbmi->biWidth,
			pbmi->biHeight,
			pbmi->biBitCount,
			//&pbmi->biCompression,
			pbmi->biSizeImage,
			pbmi->biPlanes,
			pbmi->biClrUsed);
	}
}

int sprintf_pmt(wchar_t *buffer, size_t count, char *label, const AM_MEDIA_TYPE *pmtIn)
{
	int cnt = 0;

	cnt += _snwprintf(&buffer[cnt], count - cnt, L"%S -", label);

	// char * temporalCompression = (pmtIn->bTemporalCompression) ? "Temporally compressed" : "Not temporally compressed";
	// cnt += snprintf(&buffer[cnt], count - cnt, " [%s]", temporalCompression);

	if (pmtIn->bFixedSizeSamples) {
		cnt += _snwprintf(&buffer[cnt], count - cnt, L" [Sample Size %d]", pmtIn->lSampleSize);
	}
	else {
		cnt += _snwprintf(&buffer[cnt], count - cnt, L" [Variable size samples]");
	}

	WCHAR major_uuid[64];
	WCHAR sub_uuid[64];
	StringFromGUID2(pmtIn->majortype, major_uuid, 64);
	StringFromGUID2(pmtIn->subtype, sub_uuid, 64);

	// cnt += snprintf(&buffer[cnt], count - cnt, " [%S/%S]",
	//	major_uuid,
	//	sub_uuid);

	if (pmtIn->formattype == FORMAT_VideoInfo) {

		VIDEOINFOHEADER *pVideoInfo = (VIDEOINFOHEADER *)pmtIn->pbFormat;

		// cnt += snprintf(&buffer[cnt], count - cnt, " srcRect:");
		// cnt += DisplayRECT(&buffer[cnt], count - cnt, pVideoInfo->rcSource);
		// cnt += snprintf(&buffer[cnt], count - cnt, " dstRect:");
		// cnt += DisplayRECT(&buffer[cnt], count - cnt, pVideoInfo->rcTarget);
		cnt += DisplayBITMAPINFO(&buffer[cnt], count - cnt, HEADER(pmtIn->pbFormat));

	}
	else if (pmtIn->formattype == FORMAT_VideoInfo2) {

		VIDEOINFOHEADER2 *pVideoInfo2 = (VIDEOINFOHEADER2 *)pmtIn->pbFormat;

		// cnt += snprintf(&buffer[cnt], count - cnt, " srcRect:");
		// cnt += DisplayRECT(&buffer[cnt], count - cnt, pVideoInfo2->rcSource);
		// cnt += snprintf(&buffer[cnt], count - cnt, " dstRect:");
		// cnt += DisplayRECT(&buffer[cnt], count - cnt, pVideoInfo2->rcTarget);
		cnt += DisplayBITMAPINFO(&buffer[cnt], count - cnt, &pVideoInfo2->bmiHeader);
	}
	else {
		WCHAR format_uuid[64];
		StringFromGUID2(pmtIn->formattype, format_uuid, 64);
		cnt += _snwprintf(&buffer[cnt], count - cnt, L" Format type %ls", format_uuid);
	}
	return cnt;
}

void debug_pmt(char* label, const AM_MEDIA_TYPE *pmtIn)
{
	const int SIZE = 10 * 4096;
	wchar_t buffer[SIZE];
	sprintf_pmt(buffer, SIZE, label, pmtIn);
	debug("%ls", buffer);
}

void info_pmt(char* label, const AM_MEDIA_TYPE *pmtIn)
{
	const int SIZE = 10 * 4096;
	wchar_t buffer[SIZE];
	sprintf_pmt(buffer, SIZE, label, pmtIn);
	info("%ls", buffer);
}

void error_pmt(char* label, const AM_MEDIA_TYPE *pmtIn)
{

	const int SIZE = 10 * 4096;
	wchar_t buffer[SIZE];
	sprintf_pmt(buffer, SIZE, label, pmtIn);
	error("%ls", buffer);
}

void warn_pmt(char* label, const AM_MEDIA_TYPE *pmtIn)
{

	const int SIZE = 10 * 4096;
	wchar_t buffer[SIZE];
	sprintf_pmt(buffer, SIZE, label, pmtIn);
	warn("%ls", buffer);
}

//
// CheckMediaType
// I think VLC calls this once per each enumerated media type that it likes (3 times)
// just to "make sure" that it's a real valid option
// so we could "probably" just return true here, but do some checking anyway...
//
// We will accept 8, 16, 24 or 32 bit video formats, in any
// image size that gives room to bounce.
// Returns E_INVALIDARG if the mediatype is not acceptable
//
HRESULT CPushPinDesktop::CheckMediaType(const CMediaType *pMediaType)
{
	CAutoLock cAutoLock(m_pFilter->pStateLock());
	CheckPointer(pMediaType, E_POINTER);

	const GUID Type = *(pMediaType->Type());
	if (Type != GUID_NULL && (Type != MEDIATYPE_Video) ||   // we only output video, GUID_NULL means any
		!(pMediaType->IsFixedSize()))                  // in fixed size samples
	{
		warn("CheckMediaType - E_INVALID_ARG - we only output video, GUID_NULL means any - in fixed size samples");
		return E_INVALIDARG;
	}

	// const GUID Type = *pMediaType->Type(); // always just MEDIATYPE_Video

	// Check for the subtypes we support
	if (pMediaType->Subtype() == NULL) {
		warn("CheckMediaType - E_INVALIDARG -  Subtype == NULL");
		return E_INVALIDARG;
	}

	const GUID SubType2 = *pMediaType->Subtype();

	// Get the format area of the media type
	VIDEOINFO *pvi = (VIDEOINFO *)pMediaType->Format();
	if (pvi == NULL) {
		warn("CheckMediaType - E_INVALIDARG - pvi == NULL");
		return E_INVALIDARG; // usually never this...
	}

#if 0
	// graphedit comes in with a really large one and then we set it (no good)
	if (pvi->bmiHeader.biHeight > m_iCaptureConfigHeight || pvi->bmiHeader.biWidth > m_iCaptureConfigWidth) {
		warn("CheckMediaType - E_INVALIDARG %d > %d || %d > %d",
			pvi->bmiHeader.biHeight, m_iCaptureConfigHeight,
			pvi->bmiHeader.biWidth, m_iCaptureConfigWidth);
		return E_INVALIDARG;
	}
#endif

	if ((SubType2 != MEDIASUBTYPE_RGB8) // these are all the same value? But maybe the pointers are different. Hmm.
		&& (SubType2 != MEDIASUBTYPE_RGB565)
		&& (SubType2 != MEDIASUBTYPE_RGB555)
		&& (SubType2 != MEDIASUBTYPE_RGB24)
		&& (SubType2 != MEDIASUBTYPE_RGB32)
		&& (SubType2 != GUID_NULL)) {

		if (SubType2 == WMMEDIASUBTYPE_I420) { // 30323449-0000-0010-8000-00AA00389B71 MEDIASUBTYPE_I420 == WMMEDIASUBTYPE_I420
			if (pvi->bmiHeader.biBitCount == 12) { // biCompression 808596553 == 0x30323449
												   // 12 is correct for i420 -- WFMLE uses this, VLC *can* also use it, too
			}
			else {
				warn("CheckMediaType - E_INVALIDARG invalid bit count: %d", pvi->bmiHeader.biBitCount);
				return E_INVALIDARG;
			}
		}
		else {
			if (SubType2 != MEDIASUBTYPE_YUY2 && SubType2 != MEDIASUBTYPE_UYVY) {
				OLECHAR* bstrGuid;
				StringFromCLSID(SubType2, &bstrGuid);
				// note: Chrome always asks for YUV2 and UYVY, we only support I420
				// 32595559-0000-0010-8000-00AA00389B71  MEDIASUBTYPE_YUY2, which is apparently "identical format" to I420
				// 59565955-0000-0010-8000-00AA00389B71  MEDIASUBTYPE_UYVY
				warn("CheckMediaType - E_INVALIDARG - Invalid SubType2: %S", bstrGuid);
				::CoTaskMemFree(bstrGuid);
			}
			// sometimes FLME asks for YV12 {32315659-0000-0010-8000-00AA00389B71}, or  
			// 43594448-0000-0010-8000-00AA00389B71  MEDIASUBTYPE_HDYC
			// 56555949-0000-0010-8000-00AA00389B71  MEDIASUBTYPE_IYUV # dunno if I actually get this one
			return E_INVALIDARG;
		}
	}
	else {
		// RGB's -- our default -- WFMLE doesn't get here, VLC does :P
	}

	if (m_bFormatAlreadySet) {
		// then it must be the same as our current...see SetFormat msdn
		if (m_mt == *pMediaType) {
			info("CheckMediaType - S_OK - format already set to the same type");
			return S_OK;
		}
		else {
			// FIXME: this always shows up, so setting to info instead of warn
			// info("CheckMediaType - VFW_E_TYPE_NOT_ACCEPTED format already set to different type");
			return VFW_E_TYPE_NOT_ACCEPTED;
		}
	}

	// Don't accept formats with negative height, which would cause the desktop
	// image to be displayed upside down.
	// also reject 0's, that would be weird.
	if (pvi->bmiHeader.biHeight <= 0) {
		warn("CheckMediaType - E_INVALIDARG (height: %d)", pvi->bmiHeader.biHeight);
		return E_INVALIDARG;
	}

	if (pvi->bmiHeader.biWidth <= 0) {
		warn("CheckMediaType - E_INVALIDARG (height: %d)", pvi->bmiHeader.biHeight);
		return E_INVALIDARG;
	}

	m_iCaptureConfigWidth = pvi->bmiHeader.biWidth;
	m_iCaptureConfigHeight = pvi->bmiHeader.biHeight;

	// info("%s - AvgTimePerFrame %lld", __func__, (UNITS / pvi->AvgTimePerFrame));
	// set_fps(&game_context, m_rtFrameLength * 100);

	info("CheckMediaType - S_OK - This format is acceptable.");
	return S_OK;

} // CheckMediaType


  //
  // SetMediaType
  //
  // Called when a media type is agreed between filters (i.e. they call GetMediaType+GetStreamCaps/ienumtypes I guess till they find one they like, then they call SetMediaType).
  // all this after calling Set Format, if they even do, I guess...
  // pMediaType is assumed to have passed CheckMediaType "already" and be good to go...
  // except WFMLE sends us a junk type, so we check it anyway LODO do we? Or is it the other method Set Format that they call in vain? Or it first?
HRESULT CPushPinDesktop::SetMediaType(const CMediaType *pMediaType)
{
	CAutoLock cAutoLock(m_pFilter->pStateLock());

	// Pass the call up to my base class
	HRESULT hr = CSourceStream::SetMediaType(pMediaType); // assigns our local m_mt via m_mt.Set(*pmt) ... 

	if (!SUCCEEDED(hr)) {
		error_pmt("SetMediaType - SetMediaType failed", pMediaType);
		return hr;
	}

	VIDEOINFO *pvi = (VIDEOINFO *)m_mt.Format();
	if (pvi == NULL) {
		error_pmt("SetMediaType - E_UNEXPECTED", pMediaType);
		return E_UNEXPECTED;
	}

	switch (pvi->bmiHeader.biBitCount) {

	case 12:     // i420
		hr = S_OK;
		// m_bConvertToI420 = true;
		break;
	case 8:     // 8-bit palettized
	case 16:    // RGB565, RGB555
	case 24:    // RGB24
	case 32:    // RGB32
				// Save the current media type and bit depth
				//m_MediaType = *pMediaType; // use SetMediaType above instead
		hr = S_OK;
		break;

	default:
		// We should never agree any other media types
		hr = E_INVALIDARG;
		break;
	}

	// The frame rate at which your filter should produce data is determined by the AvgTimePerFrame field of VIDEOINFOHEADER
#if 0
	(if (pvi->AvgTimePerFrame) { // or should Set Format accept this? hmm...
		m_rtFrameLength = pvi->AvgTimePerFrame; // allow them to set whatever fps they request, i.e. if it's less than the max default.  VLC command line can specify this, for instance...
		set_fps(&game_context, m_rtFrameLength * 100);
	}
#endif

	char debug_buffer[1024];
	if (hr == S_OK) {
		snprintf(debug_buffer, 1024, "SetMediaType - S_OK requested/negotiated[fps:%.02f x:%d y:%d bitcount:%d]",
			(UNITS / pvi->AvgTimePerFrame), pvi->bmiHeader.biWidth, pvi->bmiHeader.biHeight, pvi->bmiHeader.biBitCount);
		info_pmt(debug_buffer, pMediaType);
	} else {
		snprintf(debug_buffer, 1024, "SetMediaType - E_INVALIDARG [bitcount requested/negotiated: %d]", pvi->bmiHeader.biBitCount);
		error_pmt(debug_buffer, pMediaType);
	}

	return hr;

} // SetMediaType

#define DECLARE_PTR(type, ptr, expr) type* ptr = (type*)(expr);

  // sets fps, size, (etc.) maybe, or maybe just saves it away for later use...
HRESULT STDMETHODCALLTYPE CPushPinDesktop::SetFormat(AM_MEDIA_TYPE *pmt)
{
	CAutoLock cAutoLock(m_pFilter->pStateLock());


	// I *think* it can go back and forth, then.  You can call GetStreamCaps to enumerate, then call
	// SetFormat, then later calls to GetMediaType/GetStreamCaps/EnumMediatypes will all "have" to just give this one
	// though theoretically they could also call EnumMediaTypes, then Set MediaType, and not call SetFormat
	// does flash call both? what order for flash/ffmpeg/vlc calling both?
	// LODO update msdn

	// "they" [can] call this...see msdn for SetFormat

	// NULL means reset to default type...
	if (pmt != NULL)
	{
		if (pmt->formattype != FORMAT_VideoInfo) {  // FORMAT_VideoInfo == {CLSID_KsDataTypeHandlerVideo} 
													//error()
			return E_FAIL;
		}

		// LODO I should do more here...http://msdn.microsoft.com/en-us/library/dd319788.aspx I guess [meh]
		// LODO should fail if we're already streaming... [?]

		if (CheckMediaType((CMediaType *)pmt) != S_OK) {
			return E_FAIL; // just in case :P [FME...]
		}
		VIDEOINFOHEADER *pvi = (VIDEOINFOHEADER *)pmt->pbFormat;

		// for FMLE's benefit, only accept a setFormat of our "final" width [force setting via registry I guess, otherwise it only shows 80x60 whoa!]	    
		// flash media live encoder uses setFormat to determine widths [?] and then only displays the smallest? huh?
		if (pvi->bmiHeader.biWidth != getNegotiatedFinalWidth() ||
			pvi->bmiHeader.biHeight != getNegotiatedFinalHeight())
		{
			return E_INVALIDARG;
		}

		// ignore other things like cropping requests for now...

		// now save it away...for being able to re-offer it later. We could use Set MediaType but we're just being lazy and re-using m_mt for many things I guess
		m_mt = *pmt;

	}

	IPin* pin;
	ConnectedTo(&pin);
	if (pin)
	{
		IFilterGraph *pGraph = m_pParent->GetGraph();
		HRESULT res = pGraph->Reconnect(this);
		if (res != S_OK) // LODO check first, and then just re-use the old one?
			return res; // else return early...not really sure how to handle this...since we already set m_mt...but it's a pretty rare case I think...
						// plus ours is a weird case...
	}
	else {
		// graph hasn't been built yet...
		// so we're ok with "whatever" format they pass us, we're just in the setup phase...
	}

	// success of some type
	if (pmt == NULL) {
		m_bFormatAlreadySet = false;
	}
	else {
		m_bFormatAlreadySet = true;
	}

	return S_OK;
}

// get's the current format...I guess...
// or get default if they haven't called SetFormat yet...
// TODO the default, which probably we don't do yet...unless they've already called GetStreamCaps then it'll be the last index they used LOL.
HRESULT STDMETHODCALLTYPE CPushPinDesktop::GetFormat(AM_MEDIA_TYPE **ppmt)
{
	CAutoLock cAutoLock(m_pFilter->pStateLock());

	*ppmt = CreateMediaType(&m_mt); // windows internal method, also does copy
	info_pmt("GetFormat - S_OK", *ppmt);
	return S_OK;
}


HRESULT STDMETHODCALLTYPE CPushPinDesktop::GetNumberOfCapabilities(int *piCount, int *piSize)
{
	*piCount = PIN_RESOLUTION_SIZE * PIN_FPS_SIZE;
	*piSize = sizeof(VIDEO_STREAM_CONFIG_CAPS); // VIDEO_STREAM_CONFIG_CAPS is an MS struct
	info("GetNumberOfCapabilities - %d size:%d", *piCount, *piSize);
	return S_OK;
}


// returns the "range" of fps, etc. for this index
HRESULT STDMETHODCALLTYPE CPushPinDesktop::GetStreamCaps(int iIndex, AM_MEDIA_TYPE **pmt, BYTE *pSCC)
{
	CAutoLock cAutoLock(m_pFilter->pStateLock());

	HRESULT hr = GetMediaType(iIndex, &m_mt); // ensure setup/re-use m_mt ...

											  // some are indeed shared, apparently.
	if (FAILED(hr))
	{
		error("GetStreamCaps p: %d - FAILED: %x", iIndex, hr);
		return hr;
	}


	*pmt = CreateMediaType(&m_mt); // a windows lib method, also does a copy for us
	if (*pmt == NULL) {
		error("GetStreamCaps p: %d - E_OUTOFMEMORY", iIndex);
		return E_OUTOFMEMORY;
	}

	DECLARE_PTR(VIDEO_STREAM_CONFIG_CAPS, pvscc, pSCC);

	/*
	most of these are listed as deprecated by msdn... yet some still used, apparently. odd.
	*/

	int width = PIN_WIDTH[iIndex % PIN_RESOLUTION_SIZE];
	int height = PIN_HEIGHT[iIndex % PIN_RESOLUTION_SIZE];
	REFERENCE_TIME fps = PIN_FPS[iIndex / PIN_RESOLUTION_SIZE];
	int fps_n = (int)fps / UNITS;

	pvscc->VideoStandard = AnalogVideo_None;
	pvscc->InputSize.cx = width; // getCaptureDesiredFinalWidth();
	pvscc->InputSize.cy = height; //  getCaptureDesiredFinalHeight();

								  // most of these values are fakes..
	pvscc->MinCroppingSize.cx = width; //  getCaptureDesiredFinalWidth();
	pvscc->MinCroppingSize.cy = height; // getCaptureDesiredFinalHeight();

	pvscc->MaxCroppingSize.cx = width; // getCaptureDesiredFinalWidth();
	pvscc->MaxCroppingSize.cy = height; // getCaptureDesiredFinalHeight();

	pvscc->CropGranularityX = 1;
	pvscc->CropGranularityY = 1;
	pvscc->CropAlignX = 1;
	pvscc->CropAlignY = 1;

	pvscc->MinOutputSize.cx = 1;
	pvscc->MinOutputSize.cy = 1;
	pvscc->MaxOutputSize.cx = width; // getCaptureDesiredFinalWidth();
	pvscc->MaxOutputSize.cy = height; // getCaptureDesiredFinalHeight();
	pvscc->OutputGranularityX = 1;
	pvscc->OutputGranularityY = 1;

	pvscc->StretchTapsX = 1; // We do 1 tap. I guess...
	pvscc->StretchTapsY = 1;
	pvscc->ShrinkTapsX = 1;
	pvscc->ShrinkTapsY = 1;

	pvscc->MinFrameInterval = fps; // the larger default is actually the MinFrameInterval, not the max
	pvscc->MaxFrameInterval = 500000000; // 0.02 fps :) [though it could go lower, really...]

										 //    pvscc->MinBitsPerSecond = (LONG) 1*1*8*GetFps(); // if in 8 bit mode 1x1. I guess.
										 //   pvscc->MaxBitsPerSecond = (LONG) getCaptureDesiredFinalWidth()*getCaptureDesiredFinalHeight()*32*GetMaxFps() + 44; // + 44 header size? + the palette?

	pvscc->MinBitsPerSecond = (LONG)1 * 1 * 8 * fps_n; // if in 8 bit mode 1x1. I guess.
	pvscc->MaxBitsPerSecond = (LONG)width*height * 32 * fps_n + 44; // + 44 header size? + the palette?

	{
		static bool stop_logging = false;
		if (iIndex == PIN_RESOLUTION_SIZE - 1) {
			stop_logging = true;
		}

		if (!stop_logging) {
			char debug_buffer[1024];
			snprintf(debug_buffer, 1024, "GetStreamCaps S_OK p:%d", iIndex);
			debug_pmt(debug_buffer, *pmt);
		}
	}

	return hr;
}


// QuerySupported: Query whether the pin supports the specified property.
HRESULT CPushPinDesktop::QuerySupported(REFGUID guidPropSet, DWORD dwPropID, DWORD *pTypeSupport)
{
	info("QuerySupported");
	//DebugBreak();
	if (guidPropSet != AMPROPSETID_Pin) return E_PROP_SET_UNSUPPORTED;
	if (dwPropID != AMPROPERTY_PIN_CATEGORY) return E_PROP_ID_UNSUPPORTED;
	// We support getting this property, but not setting it.
	if (pTypeSupport) *pTypeSupport = KSPROPERTY_SUPPORT_GET;
	return S_OK;
}

STDMETHODIMP CGameCapture::Stop() {

	CAutoLock filterLock(m_pLock);
	info("GameCapture::Stop");

	//Default implementation
	HRESULT hr = CBaseFilter::Stop();

	//Reset pin resources
	m_pPin->m_iFrameNumber = 0;

	logRotate();
	return hr;
}


// according to msdn...
HRESULT CGameCapture::GetState(DWORD dw, FILTER_STATE *pState)
{
	CheckPointer(pState, E_POINTER);
	*pState = m_State;
	info("GetState: %d %S", m_State, m_State == State_Paused ? "VFS_S_CANT_CUE" : "S_OK");
	if (m_State == State_Paused)
		return VFW_S_CANT_CUE;
	else
		return S_OK;
}

HRESULT CPushPinDesktop::QueryInterface(REFIID riid, void **ppv)
{
	//info("GameCapture::QueryInterface");
	// Standard OLE stuff, needed for capture source
	if (riid == _uuidof(IAMStreamConfig))
		*ppv = (IAMStreamConfig*)this;
	else if (riid == _uuidof(IKsPropertySet))
		*ppv = (IKsPropertySet*)this;
	else
		return CSourceStream::QueryInterface(riid, ppv);

	AddRef(); // avoid interlocked decrement error... // I think
	return S_OK;
}



//////////////////////////////////////////////////////////////////////////
// IKsPropertySet
//////////////////////////////////////////////////////////////////////////


HRESULT CPushPinDesktop::Set(REFGUID guidPropSet, DWORD dwID, void *pInstanceData,
	DWORD cbInstanceData, void *pPropData, DWORD cbPropData)
{
	// Set: we don't have any specific properties to set...that we advertise yet anyway, and who would use them anyway?
	return E_NOTIMPL;
}

// Get: Return the pin category (our only property). 
HRESULT CPushPinDesktop::Get(
	REFGUID guidPropSet,   // Which property set.
	DWORD dwPropID,        // Which property in that set.
	void *pInstanceData,   // Instance data (ignore).
	DWORD cbInstanceData,  // Size of the instance data (ignore).
	void *pPropData,       // Buffer to receive the property data.
	DWORD cbPropData,      // Size of the buffer.
	DWORD *pcbReturned     // Return the size of the property.
)
{
	if (guidPropSet != AMPROPSETID_Pin) {
		error("Get - E_PROP_SET_UNSUPPORTED %x != AMPROPSETID_Pin", guidPropSet);
		return E_PROP_SET_UNSUPPORTED;
	}
	if (dwPropID != AMPROPERTY_PIN_CATEGORY) {
		error("Get - %d != AMPROPERTY_PIN_CATEGORY", dwPropID);
		return E_PROP_ID_UNSUPPORTED;
	}
	if (pPropData == NULL && pcbReturned == NULL) {
		error("Get - E_POINTER");
		return E_POINTER;
	}

	if (pcbReturned) *pcbReturned = sizeof(GUID);
	if (pPropData == NULL) {
		info("Get - S_OK (%d)", *pcbReturned);
		return S_OK; // Caller just wants to know the size. 
	}
	if (cbPropData < sizeof(GUID)) {
		error("Get - E_UNEXPECTED (%d)", cbPropData);
		return E_UNEXPECTED;// The buffer is too small.
	}

	*(GUID *)pPropData = PIN_CATEGORY_CAPTURE; // PIN_CATEGORY_PREVIEW ?

	info("Get - S_OK (PIN_CATEGORY_CAPTURE)");
	return S_OK;
}


enum FourCC { FOURCC_NONE = 0, FOURCC_I420 = 100, FOURCC_YUY2 = 101, FOURCC_RGB32 = 102 };// from http://www.conaito.com/docus/voip-video-evo-sdk-capi/group__videocapture.html
																						  //
																						  // GetMediaType
																						  //
																						  // Prefer 5 formats - 8, 16 (*2), 24 or 32 bits per pixel
																						  //
																						  // Prefered types should be ordered by quality, with zero as highest quality.
																						  // Therefore, iPosition =
																						  //      0    Return a 24bit mediatype "as the default" since I guessed it might be faster though who knows
																						  //      1    Return a 24bit mediatype
																						  //      2    Return 16bit RGB565
																						  //      3    Return a 16bit mediatype (rgb555)
																						  //      4    Return 8 bit palettised format
																						  //      >4   Invalid
																						  // except that we changed the orderings a bit...
																						  //
HRESULT CPushPinDesktop::GetMediaType(int iPosition, CMediaType *pmt) // AM_MEDIA_TYPE basically == CMediaType
{
	//DebugBreak();
	CheckPointer(pmt, E_POINTER);
	CAutoLock cAutoLock(m_pFilter->pStateLock());

	char debug_buffer[1024];
	//info("GameCapture::GetMediaType position:%d", iPosition);
	if (m_bFormatAlreadySet) {
		// you can only have one option, buddy, if setFormat already called. (see SetFormat's msdn)
		if (iPosition != 0) {
			error("GetMediaType - E_INVALIDARG p: %d format already set", iPosition);
			return E_INVALIDARG;
		}
		VIDEOINFO *pvi = (VIDEOINFO *)m_mt.Format();

		// Set() copies these in for us pvi->bmiHeader.biSizeImage  = GetBitmapSize(&pvi->bmiHeader); // calculates the size for us, after we gave it the width and everything else we already chucked into it
		// pmt->SetSampleSize(pvi->bmiHeader.biSizeImage);
		// nobody uses sample size anyway :P

		pmt->Set(m_mt);
		VIDEOINFOHEADER *pVih1 = (VIDEOINFOHEADER*)m_mt.pbFormat;
		VIDEOINFO *pviHere = (VIDEOINFO  *)pmt->pbFormat;
		snprintf(debug_buffer, 1024, "GetMediaType (already set) p:%d - %s", iPosition, "S_OK");
		info_pmt(debug_buffer, pmt);
		return S_OK;
	}

	// do we ever even get past here? hmm

	if (iPosition < 0) {
		error("GetMediaType - E_INVALIDARG p: %d", iPosition);
		return E_INVALIDARG;
	}

	// Have we run out of types?
	if (iPosition > PIN_RESOLUTION_SIZE * PIN_FPS_SIZE) {
		warn("GetMediaType - VFW_S_NO_MORE_ITEMS p:%d", iPosition);
		return VFW_S_NO_MORE_ITEMS;
	}

	VIDEOINFO *pvi = (VIDEOINFO *)pmt->AllocFormatBuffer(sizeof(VIDEOINFO));
	if (NULL == pvi) {
		error("GetMediaType - E_OUTOFMEMORY");
		return(E_OUTOFMEMORY);
	}

	// Initialize the VideoInfo structure before configuring its members
	ZeroMemory(pvi, sizeof(VIDEOINFO));

	int width = PIN_WIDTH[iPosition % PIN_RESOLUTION_SIZE];
	int height = PIN_HEIGHT[iPosition % PIN_RESOLUTION_SIZE];
	REFERENCE_TIME fps = PIN_FPS[iPosition / PIN_RESOLUTION_SIZE];
	LONGLONG fps_n = fps / UNITS;

	// the i420 freak-o added just for FME's benefit...
	//pvi->bmiHeader.biCompression = 0x30323449; // => ASCII "I420" is apparently right here...
	pvi->bmiHeader.biCompression = MAKEFOURCC('I', '4', '2', '0');
	pvi->bmiHeader.biBitCount = 12;
	// pvi->bmiHeader.biSizeImage = (getCaptureDesiredFinalWidth()*getCaptureDesiredFinalHeight() * 3) / 2;
	pvi->bmiHeader.biSizeImage = (width * height * 3) / 2;
	pmt->SetSubtype(&WMMEDIASUBTYPE_I420);

	// Now adjust some parameters that are the same for all formats
	pvi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	pvi->bmiHeader.biWidth = width; // getCaptureDesiredFinalWidth();
	pvi->bmiHeader.biHeight = height; // getCaptureDesiredFinalHeight();
	pvi->bmiHeader.biPlanes = 1;
	pvi->bmiHeader.biSizeImage = GetBitmapSize(&pvi->bmiHeader); // calculates the size for us, after we gave it the width and everything else we already chucked into it
	pvi->bmiHeader.biClrImportant = 0;
	pmt->SetSampleSize(pvi->bmiHeader.biSizeImage); // use the above size

	pvi->AvgTimePerFrame = PIN_FPS[iPosition / PIN_RESOLUTION_SIZE];

	SetRectEmpty(&(pvi->rcSource)); // we want the whole image area rendered.
	SetRectEmpty(&(pvi->rcTarget)); // no particular destination rectangle

	pmt->SetType(&MEDIATYPE_Video);
	pmt->SetFormatType(&FORMAT_VideoInfo);
	pmt->SetTemporalCompression(FALSE);

	// Work out the GUID for the subtype from the header info.
	if (*pmt->Subtype() == GUID_NULL) {
		const GUID SubTypeGUID = GetBitmapSubtype(&pvi->bmiHeader);
		pmt->SetSubtype(&SubTypeGUID);
	}

	// info_pmt("GetMediaType", pmt);
	return NOERROR;

} // GetMediaType