//------------------------------------------------------------------------------
// File: capture.h
//
// Desc: DirectShow sample code - In-memory push mode source filter
//
// Copyright (c) Microsoft Corporation.  All rights reserved.
//------------------------------------------------------------------------------
#ifndef CAPTURE_H
#define CAPTURE_H

#include <strsafe.h>
#include "DesktopCapture.h"
#include "GameCapture.h"
#include "GDICapture.h"
#include "CommonTypes.h"
#include "registry.h"

/*
// UNITS = 10 ^ 7  
// UNITS / 30 = 30 fps;
// UNITS / 20 = 20 fps, etc
const REFERENCE_TIME FPS_50 = UNITS / 50;
const REFERENCE_TIME FPS_30 = UNITS / 30;
const REFERENCE_TIME FPS_20 = UNITS / 20;
const REFERENCE_TIME FPS_10 = UNITS / 10;
const REFERENCE_TIME FPS_5  = UNITS / 5;
const REFERENCE_TIME FPS_4  = UNITS / 4;
const REFERENCE_TIME FPS_3  = UNITS / 3;
const REFERENCE_TIME FPS_2  = UNITS / 2;
const REFERENCE_TIME FPS_1  = UNITS / 1;
*/

// Filter name strings
#define g_wszPushDesktop    L"Bebo Game Capture Filter"
typedef unsigned __int64 QWORD;

const int CAPTURE_INJECT = 0;
const int CAPTURE_GDI = 1;
const int CAPTURE_DESKTOP = 2;
const int CAPTURE_DSHOW = 3;
const float MAX_FPS = 60;

class CPushPinDesktop;

// parent
class CGameCapture : public CSource // public IAMFilterMiscFlags // CSource is CBaseFilter is IBaseFilter is IMediaFilter is IPersist which is IUnknown
{

private:
    // Constructor is private because you have to use CreateInstance
    CGameCapture(IUnknown *pUnk, HRESULT *phr);
    ~CGameCapture();

    CPushPinDesktop *m_pPin;
public:
    //////////////////////////////////////////////////////////////////////////
    //  IUnknown
    //////////////////////////////////////////////////////////////////////////
    static CUnknown * WINAPI CreateInstance(LPUNKNOWN lpunk, HRESULT *phr);
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv);

	// ?? compiler error that these be required here? huh?
	ULONG STDMETHODCALLTYPE AddRef() { return CBaseFilter::AddRef(); };
	ULONG STDMETHODCALLTYPE Release() { return CBaseFilter::Release(); };
	
	////// 
	// IAMFilterMiscFlags, in case it helps somebody somewhere know we're a source config (probably unnecessary)
	//////
	// ULONG STDMETHODCALLTYPE GetMiscFlags() { return AM_FILTER_MISC_FLAGS_IS_SOURCE; } 
	// not sure if we should define the above without also implementing  IAMPushSource interface.

	// our own method
    IFilterGraph *GetGraph() {return m_pGraph;}

	// CBaseFilter, some pdf told me I should (msdn agrees)
	STDMETHODIMP GetState(DWORD dwMilliSecsTimeout, FILTER_STATE *State);
	STDMETHODIMP Stop(); //http://social.msdn.microsoft.com/Forums/en/windowsdirectshowdevelopment/thread/a9e62057-f23b-4ce7-874a-6dd7abc7dbf7
};


// child
class CPushPinDesktop : public CSourceStream, public IAMStreamConfig, public IKsPropertySet //CSourceStream is ... CBasePin
{

public:
    long m_iFrameNumber;

protected:
	RegKey registry;

    //int m_FramesWritten;				// To track where we are
    REFERENCE_TIME m_rtFrameLength; // also used to get the fps
	// float m_fFps; use the method to get this now
	REFERENCE_TIME previousFrame;

    int getNegotiatedFinalWidth();
    int getNegotiatedFinalHeight();                   

	game_capture_config * config;
    void * game_context;

	int m_iCaptureConfigWidth;
	int m_iCaptureConfigHeight;
	std::wstring m_pCaptureTypeName;
	std::wstring m_pCaptureId;
	std::wstring m_pCaptureLabel;
	LPWSTR m_pCaptureWindowName;
	LPWSTR m_pCaptureWindowClassName;
	LPWSTR m_pCaptureExeFullName;

	HANDLE init_hooks_thread;

	CGameCapture* m_pParent;

	DesktopCapture* m_pDesktopCapture;
	GDICapture* m_pGDICapture;

	bool m_bFormatAlreadySet;
	bool m_bCaptureOnce;
	volatile bool active;
	bool m_bCaptureAntiCheat;
	bool isBlackFrame;
	bool threadCreated;

	float GetFps();
	float GetMaxFps() { return MAX_FPS; };
	void ProcessRegistryReadEvent(long timeout);

	int m_iCaptureType;
	int m_iDesktopNumber;
	int m_iDesktopAdapterNumber;
	int getCaptureDesiredFinalWidth();
	int getCaptureDesiredFinalHeight();

	HANDLE readRegistryEvent;
	QWORD m_iCaptureHandle;
	UINT64 blackFrameCount;

public:
	
	//CSourceStream overrrides
	HRESULT OnThreadCreate(void);
	HRESULT OnThreadDestroy(void);
	HRESULT OnThreadStartPlay(void);
	int GetGameFromRegistry(void);
	void CleanupCapture();
	HRESULT Inactive(void);
	HRESULT Active(void);


    //////////////////////////////////////////////////////////////////////////
    //  IUnknown
    //////////////////////////////////////////////////////////////////////////
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv); 
    STDMETHODIMP_(ULONG) AddRef() { return GetOwner()->AddRef(); } // gets called often...
    STDMETHODIMP_(ULONG) Release() { return GetOwner()->Release(); }


     //////////////////////////////////////////////////////////////////////////
    //  IAMStreamConfig
    //////////////////////////////////////////////////////////////////////////
    HRESULT STDMETHODCALLTYPE SetFormat(AM_MEDIA_TYPE *pmt);
    HRESULT STDMETHODCALLTYPE GetFormat(AM_MEDIA_TYPE **ppmt);
    HRESULT STDMETHODCALLTYPE GetNumberOfCapabilities(int *piCount, int *piSize);
    HRESULT STDMETHODCALLTYPE GetStreamCaps(int iIndex, AM_MEDIA_TYPE **pmt, BYTE *pSCC);

    CPushPinDesktop(HRESULT *phr, CGameCapture *pFilter);
    ~CPushPinDesktop();

    // Override the version that offers exactly one media type
    HRESULT DecideBufferSize(IMemAllocator *pAlloc, ALLOCATOR_PROPERTIES *pRequest);
    HRESULT FillBuffer(IMediaSample *pSample);
    HRESULT FillBuffer_Inject(IMediaSample *pSample);
    HRESULT FillBuffer_Desktop(IMediaSample *pSample);
    HRESULT FillBuffer_GDI(IMediaSample *pSample);

    // Set the agreed media type and set up the necessary parameters
    HRESULT SetMediaType(const CMediaType *pMediaType);

    // Support multiple display formats (CBasePin)
    HRESULT CheckMediaType(const CMediaType *pMediaType);
    HRESULT GetMediaType(int iPosition, CMediaType *pmt);

    // IQualityControl
	// Not implemented because we aren't going in real time.
	// If the file-writing filter slows the graph down, we just do nothing, which means
	// wait until we're unblocked. No frames are ever dropped.
    STDMETHODIMP Notify(IBaseFilter *pSelf, Quality q)
    {
        return E_FAIL;
    }

	
    //////////////////////////////////////////////////////////////////////////
    //  IKsPropertySet
    //////////////////////////////////////////////////////////////////////////
    HRESULT STDMETHODCALLTYPE Set(REFGUID guidPropSet, DWORD dwID, void *pInstanceData, DWORD cbInstanceData, void *pPropData, DWORD cbPropData);
    HRESULT STDMETHODCALLTYPE Get(REFGUID guidPropSet, DWORD dwPropID, void *pInstanceData,DWORD cbInstanceData, void *pPropData, DWORD cbPropData, DWORD *pcbReturned);
    HRESULT STDMETHODCALLTYPE QuerySupported(REFGUID guidPropSet, DWORD dwPropID, DWORD *pTypeSupport);

private:
	HWND FindCaptureWindows(bool hwnd_must_match, QWORD captureHandle, LPWSTR className, LPWSTR windowName, LPWSTR exeName);

};

struct EnumWindowParams {
	bool find_hwnd_must_match;
	QWORD find_hwnd;
	LPWSTR find_class_name;
	LPWSTR find_window_name;
	LPWSTR find_exe_name;

	bool to_window_found;
	HWND to_capture_hwnd;
};
#endif
