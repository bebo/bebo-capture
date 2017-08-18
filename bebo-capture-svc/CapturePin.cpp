#include <streams.h>

#include <tchar.h>
#include "Capture.h"
#include "CaptureGuids.h"
#include "DibHelper.h"
#include <wmsdkidl.h>
#include "GameCapture.h"
#include "DesktopCapture.h"
#include "Logging.h"
#include "CommonTypes.h"
#include "d3d11.h"
#include <dxgi.h>
#include <Psapi.h>

#define MIN(a,b)  ((a) < (b) ? (a) : (b))  // danger! can evaluate "a" twice.

extern "C" {
	extern bool load_graphics_offsets(bool is32bit);
}

DWORD globalStart; // for some debug performance benchmarking
long countMissed = 0;
long fastestRoundMillis = 1000000; // random big number
long sumMillisTook = 0;

char out[1000];
// FIXME :  move these
bool ever_started = false;
boolean missed = false;

#ifdef _DEBUG 
int show_performance = 1;
#else
int show_performance = 0;
#endif

static DWORD WINAPI init_hooks(LPVOID unused)
{
	//	 if (USE_HOOK_ADDRESS_CACHE &&
	//		 cached_versions_match() &&
	load_graphics_offsets(true);
	load_graphics_offsets(false);
	return 0;
}

// the default child constructor...
CPushPinDesktop::CPushPinDesktop(HRESULT *phr, CGameCapture *pFilter)
	: CSourceStream(NAME("Push Source CPushPinDesktop child/pin"), phr, pFilter, L"Capture"),
	m_iFrameNumber(0),
	pOldData(NULL),
	m_bConvertToI420(true),
	m_pParent(pFilter),
	m_bFormatAlreadySet(false),
	hRawBitmap(NULL),
	m_bUseCaptureBlt(false),
	previousFrame(0),
	active(false),
	m_pCaptureWindowName(NULL),
	m_pCaptureWindowClassName(NULL),
	m_pCaptureExeFullName(NULL),
	game_context(NULL),
    m_iCaptureType(CAPTURE_INJECT),
	m_pDesktopCapture(new DesktopCapture),
	m_pGDICapture(new GDICapture),
	m_iDesktopNumber(-1),
	m_iDesktopAdapterNumber(-1),
	m_iCaptureHandle(-1),
	m_bCaptureOnce(0)
{
	info("CPushPinDesktop");
	// Get the device context of the main display, just to get some metrics for it...
	config = (struct game_capture_config*) malloc(sizeof game_capture_config);
	memset(config, 0, sizeof game_capture_config);

	init_hooks_thread = CreateThread(NULL, 0, init_hooks, NULL, 0, NULL);

	// Get the dimensions of the main desktop window as the default
	m_rScreen.left = m_rScreen.top = 0;
	m_rScreen.right = GetDeviceCaps(hScrDc, HORZRES); // NB this *fails* for dual monitor support currently... but we just get the wrong width by default, at least with aero windows 7 both can capture both monitors
	m_rScreen.bottom = GetDeviceCaps(hScrDc, VERTRES);

	// now read some custom settings...
	WarmupCounter();

	int config_width = read_config_setting(TEXT("MaxCaptureWidth"), 1920, true);
	ASSERT_RAISE(config_width >= 0); // negatives not allowed...
	info("MaxCaptureWidth: %d", config_width);
	int config_height = read_config_setting(TEXT("MaxCaptureHeight"), 1080, true);
	ASSERT_RAISE(config_height >= 0); // negatives not allowed, if it's set :)
	info("MaxCaptureHeight: %d", config_height);

	if (config_width > 0) {
		int desired = m_rScreen.left + config_width;
		//int max_possible = m_rScreen.right; // disabled check until I get dual monitor working. or should I allow off screen captures anyway?
		//if(desired < max_possible)
		m_rScreen.right = desired;
		//else
		//	m_rScreen.right = max_possible;
	}
	else {
		// leave full screen
	}

	m_iCaptureConfigWidth = m_rScreen.right - m_rScreen.left;
	ASSERT_RAISE(m_iCaptureConfigWidth > 0);

	if (config_height > 0) {
		int desired = m_rScreen.top + config_height;
		//int max_possible = m_rScreen.bottom; // disabled, see above.
		//if(desired < max_possible)
		m_rScreen.bottom = desired;
		//else
		//	m_rScreen.bottom = max_possible;
	}
	else {
		// leave full screen
	}
	m_iCaptureConfigHeight = m_rScreen.bottom - m_rScreen.top;
	ASSERT_RAISE(m_iCaptureConfigHeight > 0);

	// default 30 fps...hmm...
	int config_max_fps = read_config_setting(TEXT("MaxCaptureFPS"), 60, false);
	ASSERT_RAISE(config_max_fps > 0);	
	info("MaxCaptureFPS: %d", config_max_fps);

	// m_rtFrameLength is also re-negotiated later...
	m_rtFrameLength = UNITS / config_max_fps;

	GetGameFromRegistry();

	LOGF(INFO, "default/from reg read config as: %dx%d -> %dx%d (%d top %d bottom %d l %d r) %dfps",
		m_iCaptureConfigHeight, m_iCaptureConfigWidth, getCaptureDesiredFinalHeight(), getCaptureDesiredFinalWidth(), m_rScreen.top, m_rScreen.bottom, m_rScreen.left, m_rScreen.right, config_max_fps);
}

CPushPinDesktop::~CPushPinDesktop()
{
	delete m_pDesktopCapture;
	m_pDesktopCapture = nullptr;

	delete m_pGDICapture;
	m_pGDICapture = nullptr;

	// They *should* call this...VLC does at least, correctly.

	// Release the device context stuff
	LOG(INFO) << "Total no. Frames written: " << m_iFrameNumber << " " << out;
    logRotate();

	// release desktop capture

	if (hRawBitmap)
		DeleteObject(hRawBitmap); // don't need those bytes anymore -- I think we are supposed to delete just this and not hOldBitmap

	if (pOldData) {
		free(pOldData);
		pOldData = NULL;
	}
}

void CPushPinDesktop::GetGameFromRegistry(void) {
	DWORD size = 1024;
	BYTE data[1024];

	if (RegGetBeboSZ(TEXT("CaptureType"), data, &size) == S_OK) {
		int old = m_iCaptureType;
		char type[1024];
		sprintf(type, "%S", data);
		if (strcmp(type, "desktop") == 0) {
			m_iCaptureType = CAPTURE_DESKTOP;
		} else if (strcmp(type, "inject") == 0) {
			m_iCaptureType = CAPTURE_INJECT;
		} else if (strcmp(type, "gdi") == 0) {
			m_iCaptureType = CAPTURE_GDI;
		} else if (strcmp(type, "dshow") == 0) {
			m_iCaptureType = CAPTURE_DSHOW;
		}
		if (old != m_iCaptureType) {
			info("CaptureType: %s (%d)", type, m_iCaptureType);
		}
	}
    
	size = sizeof(data);
	int oldAdapterId = m_iDesktopAdapterNumber;
	int oldDesktopId = m_iDesktopNumber;
	if (RegGetBeboSZ(TEXT("CaptureId"), data, &size) == S_OK) {
		char text[1024];
		sprintf(text, "%S", data);
		char * typeName = strtok(text, ":");
		char * adapterId = strtok(NULL, ":");
		char * desktopId = strtok(NULL, ":");

		if (adapterId != NULL) {
			if (strcmp(typeName, "desktop") == 0) {
				m_iDesktopAdapterNumber = atoi(adapterId);
			}
		}
		if (desktopId != NULL) {
			if (strcmp(typeName, "desktop") == 0) {
				m_iDesktopNumber = atoi(desktopId);
			}
		}

		if (oldAdapterId != m_iDesktopAdapterNumber ||
			oldDesktopId != m_iDesktopNumber) {
			info("CaptureId: %s:%s:%s", typeName, adapterId, desktopId);
		}
	}

	size = sizeof(data);
	if (RegGetBeboSZ(TEXT("CaptureWindowName"), data, &size) == S_OK) {
		LPWSTR old = m_pCaptureWindowName;
		m_pCaptureWindowName = (LPWSTR) malloc(size*2);
		wsprintfW(m_pCaptureWindowName, L"%s", data);
		if (old == NULL || wcscmp(old, m_pCaptureWindowName) != 0) {
			info("CaptureWindowName: %S", m_pCaptureWindowName);
			if (old != NULL) {
				free(old);
			}
		}
	}

	size = sizeof(data);
	if (RegGetBeboSZ(TEXT("CaptureWindowClassName"), data, &size) == S_OK) {
		LPWSTR old = m_pCaptureWindowClassName;
		m_pCaptureWindowClassName = (LPWSTR) malloc(size * 2);
		wsprintfW(m_pCaptureWindowClassName, L"%s", data);
		if (old == NULL || wcscmp(old, m_pCaptureWindowClassName) != 0) {
			info("CaptureWindowClassName: %S", m_pCaptureWindowClassName);
			if (old != NULL) {
				free(old);
			}
		}
	}

	size = sizeof(data);
	if (RegGetBeboSZ(TEXT("CaptureExeFullName"), data, &size) == S_OK) {
		LPWSTR old = m_pCaptureExeFullName;
		m_pCaptureExeFullName = (LPWSTR) malloc(size * 2);
		wsprintfW(m_pCaptureExeFullName, L"%s", data);
		if (old == NULL || wcscmp(old, m_pCaptureExeFullName) != 0) {
			info("m_pCaptureExeFullName: %S", m_pCaptureExeFullName);
			if (old != NULL) {
				free(old);
			}
		}
	}

	QWORD qout;
	QWORD oldCaptureHandle = m_iCaptureHandle;
	if (RegGetBeboQWord(TEXT("CaptureWindowHandle"), &qout) == S_OK) {
		m_iCaptureHandle = qout;
		if (oldCaptureHandle != m_iCaptureHandle) {
			info("CaptureWindowHandle: %ld", m_iCaptureHandle);
		}
	}

	int oldAntiCheat = m_bCaptureAntiCheat;
	m_bCaptureAntiCheat = read_config_setting(TEXT("CaptureAntiCheat"), 0, true) == 1;
	if (oldAntiCheat != m_bCaptureAntiCheat) {
		info("CaptureAntiCheat: %d", m_bCaptureAntiCheat);
	}

	int oldCaptureOnce = m_bCaptureOnce;
	m_bCaptureOnce = read_config_setting(TEXT("CaptureOnce"), 0, true) == 1;
	if (oldCaptureOnce != m_bCaptureOnce) {
		info("CaptureOnce: %d", m_bCaptureOnce);
	}
	return;
}


HRESULT CPushPinDesktop::Inactive(void) {
	active = false;
	return CSourceStream::Inactive();
};

HRESULT CPushPinDesktop::Active(void) {
	active = true;
	return CSourceStream::Active();
};

HRESULT CPushPinDesktop::FillBuffer_Desktop(IMediaSample *pSample) {
	CheckPointer(pSample, E_POINTER);

	while (!m_pDesktopCapture->IsReady() && !m_pDesktopCapture->ExceedMaxRetry()) {
		info("Initializing desktop capture - adapter: %d, desktop: %d, size: %dx%d",
			m_iDesktopAdapterNumber, m_iDesktopNumber, getNegotiatedFinalWidth(), getNegotiatedFinalHeight());
		m_pDesktopCapture->Init(m_iDesktopAdapterNumber, m_iDesktopNumber, getNegotiatedFinalWidth(), getNegotiatedFinalHeight());

		globalStart = GetTickCount();
		countMissed = 0;
		sumMillisTook = 0;
		fastestRoundMillis = LONG_MAX;
		m_iFrameNumber = 0;
		missed = true;
		previousFrame = 0;
		debug("frame_length: %d", m_rtFrameLength);
	}

	if (!m_pDesktopCapture->IsReady() && m_pDesktopCapture->ExceedMaxRetry()) {
		error("Unable to initialize desktop capture. Maximum retry exceeded.");
		return S_FALSE;
	}

	__int64 startThisRound = StartCounter();

	long double millisThisRoundTook = 0;
	CRefTime now;
	now = 0;
	
	bool frame = false;
	while (!frame) {
		if (!active) {
			info("inactive - fillbuffer_desktop");
			return S_FALSE;
		}
		CSourceStream::m_pFilter->StreamTime(now);
		if (now <= 0) {
			DWORD dwMilliseconds = (DWORD)(m_rtFrameLength / 10000L);
			debug("no reference graph clock - sleeping %d", dwMilliseconds);
			Sleep(dwMilliseconds);
		} else if (now < (previousFrame + m_rtFrameLength)) {
			DWORD dwMilliseconds = (DWORD)max(1, min((previousFrame + m_rtFrameLength - now), m_rtFrameLength) / 10000L);
			debug("sleeping - %d", dwMilliseconds);
			Sleep(dwMilliseconds);
		} else if (missed) {
			DWORD dwMilliseconds = (DWORD)(m_rtFrameLength / 20000L);
			debug("starting/missed - sleeping %d", dwMilliseconds);
			Sleep(dwMilliseconds);
			CSourceStream::m_pFilter->StreamTime(now);
		} else if (now > (previousFrame + 2 * m_rtFrameLength)) {
			int missed_nr = (now - m_rtFrameLength - previousFrame) / m_rtFrameLength;
			m_iFrameNumber += missed_nr;
			countMissed += missed_nr;
			debug("missed %d frames can't keep up %d %d %.02f %llf %llf %11f",
				missed_nr, m_iFrameNumber, countMissed, (100.0L*countMissed / m_iFrameNumber), 0.0001 * now, 0.0001 * previousFrame, 0.0001 * (now - m_rtFrameLength - previousFrame));
			previousFrame = previousFrame + missed_nr * m_rtFrameLength;
			missed = true;
		}

		startThisRound = StartCounter();
		frame = m_pDesktopCapture->GetFrame(pSample, false, now);

		if (!frame && missed && now > (previousFrame + 10000000L / 5)) {
			debug("fake frame");
			countMissed += 1;
			frame = m_pDesktopCapture->GetOldFrame(pSample, false);
		}
		if (frame && previousFrame <= 0) {
			frame = false;
			previousFrame = now;
			missed = false;
			debug("skip first frame");
		} 

	}
	missed = false;
	millisThisRoundTook = GetCounterSinceStartMillis(startThisRound);
	fastestRoundMillis = min(millisThisRoundTook, fastestRoundMillis);
	sumMillisTook += millisThisRoundTook;

	// accomodate for 0 to avoid startup negatives, which would kill our math on the next loop...
	previousFrame = max(0, previousFrame);
	// auto-correct drift
	previousFrame = previousFrame + m_rtFrameLength;

	REFERENCE_TIME startFrame = m_iFrameNumber * m_rtFrameLength;
	REFERENCE_TIME endFrame = startFrame + m_rtFrameLength;
	pSample->SetTime((REFERENCE_TIME *)&startFrame, (REFERENCE_TIME *)&endFrame);
	CSourceStream::m_pFilter->StreamTime(now);	
	debug("timestamping (%11f) video packet %llf -> %llf length:(%11f) drift:(%llf)", 0.0001 * now, 0.0001 * startFrame, 0.0001 * endFrame, 0.0001 * (endFrame - startFrame), 0.0001 * (now - previousFrame));

	m_iFrameNumber++;

	// Set TRUE on every sample for uncompressed frames http://msdn.microsoft.com/en-us/library/windows/desktop/dd407021%28v=vs.85%29.aspx
	pSample->SetSyncPoint(TRUE);

	// only set discontinuous for the first...I think...
	pSample->SetDiscontinuity(m_iFrameNumber <= 1);

	double m_fFpsSinceBeginningOfTime = ((double)m_iFrameNumber) / (GetTickCount() - globalStart) * 1000;
	sprintf(out, "done video frame! total frames: %d this one %dx%d -> (%dx%d) took: %.02Lfms, %.02f ave fps (%.02f is the theoretical max fps based on this round, ave. possible fps %.02f, fastest round fps %.02f, negotiated fps %.06f), frame missed %d",
		m_iFrameNumber, m_iCaptureConfigWidth, m_iCaptureConfigHeight, getNegotiatedFinalWidth(), getNegotiatedFinalHeight(), millisThisRoundTook, m_fFpsSinceBeginningOfTime, 1.0 * 1000 / millisThisRoundTook,
		/* average */ 1.0 * 1000 * m_iFrameNumber / sumMillisTook, 1.0 * 1000 / fastestRoundMillis, GetFps(), countMissed);
	debug(out);
	return S_OK;
}

HRESULT CPushPinDesktop::FillBuffer(IMediaSample *pSample)
{
	__int64 startThisRound = StartCounter();

	CheckPointer(pSample, E_POINTER);

	long double millisThisRoundTook = 0;
	CRefTime now;
	now = 0;

	boolean gotFrame = false;
	while (!gotFrame) {
		switch (m_iCaptureType) {
		case CAPTURE_DESKTOP:
			return FillBuffer_Desktop(pSample);
		case CAPTURE_INJECT:
			break;
		case CAPTURE_GDI:
			return FillBuffer_GDI(pSample);
		case CAPTURE_DSHOW:
			error("LIBDSHOW CAPTURE IS NOT SUPPRTED YET");
			break;
		default:
			error("UNKNOWN CAPTURE TYPE: %d", m_iCaptureType);
		}
		// IsStopped() is not set until we have returned, so we need to do a peek to exit or we will never stop
		if (!active) {
			info("FillBuffer - inactive");
			return S_FALSE;
		}

		if (!isReady(&game_context)) {

			config->scale_cx = m_iCaptureConfigWidth;
			config->scale_cy = m_iCaptureConfigHeight;
			config->force_scaling = 1;
			config->anticheat_hook = m_bCaptureAntiCheat;

			game_context = hook(&game_context, m_pCaptureWindowClassName, m_pCaptureWindowName, config, m_rtFrameLength * 100);
			if (!isReady(&game_context)) {
				Sleep(50);
				GetGameFromRegistry();
				continue;
			}

			// reset stats - stream really starts here
			globalStart = GetTickCount();
			countMissed = 0;
			sumMillisTook = 0;
			fastestRoundMillis = LONG_MAX;
			m_iFrameNumber = 0;
			missed = true;
			previousFrame = 0;
			debug("frame_length: %d", m_rtFrameLength);

		}
		CSourceStream::m_pFilter->StreamTime(now);

		if (now <= 0) {
			DWORD dwMilliseconds = (DWORD)(m_rtFrameLength / 20000L);
			debug("no reference graph clock - sleeping %d", dwMilliseconds);
			Sleep(dwMilliseconds);
		}
		else if (now < (previousFrame + (m_rtFrameLength / 2))) {
			DWORD dwMilliseconds = (DWORD)max(1, min(10000 + previousFrame + (m_rtFrameLength / 2) - now, (m_rtFrameLength / 2)) / 10000L);
			debug("sleeping A - %d", dwMilliseconds);
			Sleep(dwMilliseconds);
		}
		else if (now < (previousFrame + m_rtFrameLength)) {
			DWORD dwMilliseconds = (DWORD)max(1, min((previousFrame + m_rtFrameLength - now), (m_rtFrameLength / 2)) / 10000L);
			debug("sleeping B - %d", dwMilliseconds);
			Sleep(dwMilliseconds);
		}
		else if (missed) {
			DWORD dwMilliseconds = (DWORD)(m_rtFrameLength / 10000L);
			debug("starting/missed - sleeping %d", dwMilliseconds);
			Sleep(dwMilliseconds);
			CSourceStream::m_pFilter->StreamTime(now);
		}
		else if (missed == false && m_iFrameNumber == 0) {
			debug("getting second frame");
			missed = true;
		}
		else if (now > (previousFrame + 2 * m_rtFrameLength)) {
			int missed_nr = (now - m_rtFrameLength - previousFrame) / m_rtFrameLength;
			m_iFrameNumber += missed_nr;
			countMissed += missed_nr;
			debug("missed %d frames can't keep up %d %d %.02f %llf %llf %11f",
				missed_nr, m_iFrameNumber, countMissed, (100.0L*countMissed / m_iFrameNumber), 0.0001 * now, 0.0001 * previousFrame, 0.0001 * (now - m_rtFrameLength - previousFrame));
			previousFrame = previousFrame + missed_nr * m_rtFrameLength;
			missed = true;
		}
		else {
			debug("late need to catch up");
			missed = true;
		}

		startThisRound = StartCounter();
		gotFrame = get_game_frame(&game_context, missed, pSample);
		if (!game_context) {
			gotFrame = false;
			info("Capture Ended");
		}

		if (gotFrame && previousFrame <= 0) {
			gotFrame = false;
			previousFrame = now;
			missed = false;
			debug("skip first frame");
		}
	}
	missed = false;
	millisThisRoundTook = GetCounterSinceStartMillis(startThisRound);
	fastestRoundMillis = min(millisThisRoundTook, fastestRoundMillis);
	sumMillisTook += millisThisRoundTook;

	// accomodate for 0 to avoid startup negatives, which would kill our math on the next loop...
	previousFrame = max(0, previousFrame);
	// auto-correct drift
	previousFrame = previousFrame + m_rtFrameLength;

	REFERENCE_TIME startFrame = m_iFrameNumber * m_rtFrameLength;
	REFERENCE_TIME endFrame = startFrame + m_rtFrameLength;
	pSample->SetTime((REFERENCE_TIME *)&startFrame, (REFERENCE_TIME *)&endFrame);
	CSourceStream::m_pFilter->StreamTime(now);
	debug("timestamping (%11f) video packet %llf -> %llf length:(%11f) drift:(%llf)", 0.0001 * now, 0.0001 * startFrame, 0.0001 * endFrame, 0.0001 * (endFrame - startFrame), 0.0001 * (now - previousFrame));

	m_iFrameNumber++;

	// Set TRUE on every sample for uncompressed frames http://msdn.microsoft.com/en-us/library/windows/desktop/dd407021%28v=vs.85%29.aspx
	pSample->SetSyncPoint(TRUE);

	// only set discontinuous for the first...I think...
	pSample->SetDiscontinuity(m_iFrameNumber <= 1);

	double m_fFpsSinceBeginningOfTime = ((double)m_iFrameNumber) / (GetTickCount() - globalStart) * 1000;
	sprintf(out, "done video frame! total frames: %d this one %dx%d -> (%dx%d) took: %.02Lfms, %.02f ave fps (%.02f is the theoretical max fps based on this round, ave. possible fps %.02f, fastest round fps %.02f, negotiated fps %.06f), frame missed %d",
		m_iFrameNumber, m_iCaptureConfigWidth, m_iCaptureConfigHeight, getNegotiatedFinalWidth(), getNegotiatedFinalHeight(), millisThisRoundTook, m_fFpsSinceBeginningOfTime, 1.0 * 1000 / millisThisRoundTook,
		/* average */ 1.0 * 1000 * m_iFrameNumber / sumMillisTook, 1.0 * 1000 / fastestRoundMillis, GetFps(), countMissed);
	debug(out);
	return S_OK;
}

HRESULT CPushPinDesktop::FillBuffer_GDI(IMediaSample *pSample)
{
	CheckPointer(pSample, E_POINTER);

	while (!m_pGDICapture->IsReady()) {
		debug("GDI Init - capture once: %d, handle: %ld, class name: %S, window name: %S, exe full name: %S", 
			m_bCaptureOnce, m_iCaptureHandle, m_pCaptureWindowClassName, m_pCaptureWindowName, m_pCaptureExeFullName);
		HWND hwnd = FindCaptureWindows(m_bCaptureOnce, m_iCaptureHandle, m_pCaptureWindowClassName, m_pCaptureWindowName, m_pCaptureExeFullName);
		if (hwnd == NULL) {
			error("Unable to initialize gdi capture. window not found.");
			return S_FALSE;
		}
		m_pGDICapture->InitHDC(getNegotiatedFinalWidth(), getNegotiatedFinalHeight(), hwnd);

		globalStart = GetTickCount();
		countMissed = 0;
		sumMillisTook = 0;
		fastestRoundMillis = LONG_MAX;
		m_iFrameNumber = 0;
		missed = true;
		previousFrame = 0;
		debug("frame_length: %d", m_rtFrameLength);
	}

	if (!m_pGDICapture->IsReady()) {
		error("Unable to initialize gdi capture. Maximum retry exceeded.");
		return S_FALSE;
	}

	__int64 startThisRound = StartCounter();

	long double millisThisRoundTook = 0;
	CRefTime now;
	now = 0;
	
	bool frame = false;
	while (!frame) {
		if (!active) {
			info("inactive - fillbuffer_gdi");
			return S_FALSE;
		}
		CSourceStream::m_pFilter->StreamTime(now);
		if (now <= 0) {
			DWORD dwMilliseconds = (DWORD)(m_rtFrameLength / 10000L);
			debug("no reference graph clock - sleeping %d", dwMilliseconds);
			Sleep(dwMilliseconds);
		} else if (now < (previousFrame + m_rtFrameLength)) {
			DWORD dwMilliseconds = (DWORD)max(1, min((previousFrame + m_rtFrameLength - now), m_rtFrameLength) / 10000L);
			debug("sleeping - %d", dwMilliseconds);
			Sleep(dwMilliseconds);
		} else if (missed) {
			DWORD dwMilliseconds = (DWORD)(m_rtFrameLength / 20000L);
			debug("starting/missed - sleeping %d", dwMilliseconds);
			Sleep(dwMilliseconds);
			CSourceStream::m_pFilter->StreamTime(now);
		} else if (now > (previousFrame + 2 * m_rtFrameLength)) {
			int missed_nr = (now - m_rtFrameLength - previousFrame) / m_rtFrameLength;
			m_iFrameNumber += missed_nr;
			countMissed += missed_nr;
			debug("missed %d frames can't keep up %d %d %.02f %llf %llf %11f",
				missed_nr, m_iFrameNumber, countMissed, (100.0L*countMissed / m_iFrameNumber), 0.0001 * now, 0.0001 * previousFrame, 0.0001 * (now - m_rtFrameLength - previousFrame));
			previousFrame = previousFrame + missed_nr * m_rtFrameLength;
			missed = true;
		}

		startThisRound = StartCounter();
		frame = m_pGDICapture->GetFrame(pSample);

		if (!frame) {
			if (!IsWindow(m_pGDICapture->capturingHwnd())) {
				info("window is no longer exists.");
				return S_FALSE;
			}
			/*
			if (missed && now > (previousFrame + 10000000L / 5)) {
				debug("fake frame");
				countMissed += 1;
				frame = m_pGDICapture->GetOldFrame(pSample);
			}
			*/
		}

		if (frame && previousFrame <= 0) {
			frame = false;
			previousFrame = now;
			missed = false;
			debug("skip first frame");
		} 

	}
	missed = false;
	millisThisRoundTook = GetCounterSinceStartMillis(startThisRound);
	fastestRoundMillis = min(millisThisRoundTook, fastestRoundMillis);
	sumMillisTook += millisThisRoundTook;

	// accomodate for 0 to avoid startup negatives, which would kill our math on the next loop...
	previousFrame = max(0, previousFrame);
	// auto-correct drift
	previousFrame = previousFrame + m_rtFrameLength;

	REFERENCE_TIME startFrame = m_iFrameNumber * m_rtFrameLength;
	REFERENCE_TIME endFrame = startFrame + m_rtFrameLength;
	pSample->SetTime((REFERENCE_TIME *)&startFrame, (REFERENCE_TIME *)&endFrame);
	CSourceStream::m_pFilter->StreamTime(now);	
	debug("timestamping (%11f) video packet %llf -> %llf length:(%11f) drift:(%llf)", 0.0001 * now, 0.0001 * startFrame, 0.0001 * endFrame, 0.0001 * (endFrame - startFrame), 0.0001 * (now - previousFrame));

	m_iFrameNumber++;

	// Set TRUE on every sample for uncompressed frames http://msdn.microsoft.com/en-us/library/windows/desktop/dd407021%28v=vs.85%29.aspx
	pSample->SetSyncPoint(TRUE);

	// only set discontinuous for the first...I think...
	pSample->SetDiscontinuity(m_iFrameNumber <= 1);

	double m_fFpsSinceBeginningOfTime = ((double)m_iFrameNumber) / (GetTickCount() - globalStart) * 1000;
	sprintf(out, "done video frame! total frames: %d this one %dx%d -> (%dx%d) took: %.02Lfms, %.02f ave fps (%.02f is the theoretical max fps based on this round, ave. possible fps %.02f, fastest round fps %.02f, negotiated fps %.06f), frame missed %d",
		m_iFrameNumber, m_iCaptureConfigWidth, m_iCaptureConfigHeight, getNegotiatedFinalWidth(), getNegotiatedFinalHeight(), millisThisRoundTook, m_fFpsSinceBeginningOfTime, 1.0 * 1000 / millisThisRoundTook,
		/* average */ 1.0 * 1000 * m_iFrameNumber / sumMillisTook, 1.0 * 1000 / fastestRoundMillis, GetFps(), countMissed);
	debug(out);
	return S_OK;
}

float CPushPinDesktop::GetFps() {
	return (float)(UNITS / m_rtFrameLength);
}

int CPushPinDesktop::getNegotiatedFinalWidth() {
	int iImageWidth = m_rScreen.right - m_rScreen.left;
	ASSERT_RAISE(iImageWidth > 0);
	return iImageWidth;
}

int CPushPinDesktop::getNegotiatedFinalHeight() {
	// might be smaller than the "getCaptureDesiredFinalWidth" if they tell us to give them an even smaller setting...
	int iImageHeight = m_rScreen.bottom - m_rScreen.top;
	ASSERT_RAISE(iImageHeight > 0);
	return iImageHeight;
}

int CPushPinDesktop::getCaptureDesiredFinalWidth() {
	debug("getCaptureDesiredFinalWidth: %d", m_iCaptureConfigWidth);
	return m_iCaptureConfigWidth; // full/config setting, static
}

int CPushPinDesktop::getCaptureDesiredFinalHeight() {
	debug("getCaptureDesiredFinalHeight: %d", m_iCaptureConfigHeight);
	return m_iCaptureConfigHeight; // defaults to full/config static
}

//
// DecideBufferSize
//
// This will always be called after the format has been sucessfully
// negotiated (this is negotiatebuffersize). So we have a look at m_mt to see what size image we agreed.
// Then we can ask for buffers of the correct size to contain them.
//
HRESULT CPushPinDesktop::DecideBufferSize(IMemAllocator *pAlloc,
	ALLOCATOR_PROPERTIES *pProperties)
{
	//DebugBreak();
	CheckPointer(pAlloc, E_POINTER);
	CheckPointer(pProperties, E_POINTER);

	CAutoLock cAutoLock(m_pFilter->pStateLock());
	HRESULT hr = NOERROR;

	VIDEOINFO *pvi = (VIDEOINFO *)m_mt.Format();
	BITMAPINFOHEADER header = pvi->bmiHeader;

	ASSERT_RETURN(header.biPlanes == 1); // sanity check
	// ASSERT_RAISE(header.biCompression == 0); // meaning "none" sanity check, unless we are allowing for BI_BITFIELDS [?] so leave commented out for now
	// now try to avoid this crash [XP, VLC 1.1.11]: vlc -vvv dshow:// :dshow-vdev="bebo-game-capture" :dshow-adev --sout  "#transcode{venc=theora,vcodec=theo,vb=512,scale=0.7,acodec=vorb,ab=128,channels=2,samplerate=44100,audio-sync}:standard{access=file,mux=ogg,dst=test.ogv}" with 10x10 or 1000x1000
	// LODO check if biClrUsed is passed in right for 16 bit [I'd guess it is...]
	// pProperties->cbBuffer = pvi->bmiHeader.biSizeImage; // too small. Apparently *way* too small.

	int bytesPerLine;
	// there may be a windows method that would do this for us...GetBitmapSize(&header); but might be too small for VLC? LODO try it :)
	// some pasted code...
	int bytesPerPixel = (header.biBitCount / 8);
	if (m_bConvertToI420) {
		bytesPerPixel = 32 / 8; // we convert from a 32 bit to i420, so need more space in this case
	}

	bytesPerLine = header.biWidth * bytesPerPixel;
	/* round up to a dword boundary for stride */
	if (bytesPerLine & 0x0003)
	{
		bytesPerLine |= 0x0003;
		++bytesPerLine;
	}

	ASSERT_RETURN(header.biHeight > 0); // sanity check
	ASSERT_RETURN(header.biWidth > 0); // sanity check
	// NB that we are adding in space for a final "pixel array" (http://en.wikipedia.org/wiki/BMP_file_format#DIB_Header_.28Bitmap_Information_Header.29) even though we typically don't need it, this seems to fix the segfaults
	// maybe somehow down the line some VLC thing thinks it might be there...weirder than weird.. LODO debug it LOL.
	int bitmapSize = 14 + header.biSize + (long)(bytesPerLine)*(header.biHeight) + bytesPerLine*header.biHeight;
	pProperties->cbBuffer = bitmapSize;
	//pProperties->cbBuffer = max(pProperties->cbBuffer, m_mt.GetSampleSize()); // didn't help anything
	if (m_bConvertToI420) {
		pProperties->cbBuffer = header.biHeight * header.biWidth * 3 / 2; // necessary to prevent an "out of memory" error for FMLE. Yikes. Oh wow yikes.
	}

	pProperties->cBuffers = 1; // 2 here doesn't seem to help the crashes...

	// Ask the allocator to reserve us some sample memory. NOTE: the function
	// can succeed (return NOERROR) but still not have allocated the
	// memory that we requested, so we must check we got whatever we wanted.
	ALLOCATOR_PROPERTIES Actual;
	hr = pAlloc->SetProperties(pProperties, &Actual);
	if (FAILED(hr))
	{
		return hr;
	}

	// Is this allocator unsuitable?
	if (Actual.cbBuffer < pProperties->cbBuffer)
	{
		return E_FAIL;
	}

	// now some "once per run" setups

	// LODO reset aer with each run...somehow...somehow...Stop method or something...
	OSVERSIONINFOEX version;
	ZeroMemory(&version, sizeof(OSVERSIONINFOEX));
	version.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
	GetVersionEx((LPOSVERSIONINFO)&version);
	if (version.dwMajorVersion >= 6) { // meaning vista +
		if (read_config_setting(TEXT("disable_aero_for_vista_plus_if_1"), 0, true) == 1) {
			printf("turning aero off/disabling aero");
			turnAeroOn(false);
		}
		else {
			printf("leaving aero on");
			turnAeroOn(true);
		}
	}

	if (pOldData) {
		free(pOldData);
		pOldData = NULL;
	}
	pOldData = (BYTE *)malloc(max(pProperties->cbBuffer*pProperties->cBuffers, bitmapSize)); // we convert from a 32 bit to i420, so need more space, hence max
	memset(pOldData, 0, pProperties->cbBuffer*pProperties->cBuffers); // reset it just in case :P	

	// create a bitmap compatible with the screen DC
	if (hRawBitmap)
		DeleteObject(hRawBitmap); // delete the old one in case it exists...
	hRawBitmap = CreateCompatibleBitmap(hScrDc, getNegotiatedFinalWidth(), getNegotiatedFinalHeight());

	previousFrame = 0; // reset
	m_iFrameNumber = 0;

	return NOERROR;
} // DecideBufferSize


HRESULT CPushPinDesktop::OnThreadCreate() {
	info("CPushPinDesktop OnThreadCreate");
	previousFrame = 0; // reset <sigh> dunno if this helps FME which sometimes had inconsistencies, or not
	m_iFrameNumber = 0;
	return S_OK;
}

HRESULT CPushPinDesktop::OnThreadDestroy() {
	debug("CPushPinDesktop::OnThreadDestroy()");
	if (game_context) {
		stop_game_capture(&game_context);
		game_context = NULL;
	}
	return NOERROR;
};

HRESULT CPushPinDesktop::OnThreadStartPlay() {
	debug("CPushPinDesktop::OnThreadStartPlay()");
	return NOERROR;
};

BOOL CALLBACK WindowsProcVerifier(HWND hwnd, LPARAM param)
{
	EnumWindowParams* p = reinterpret_cast<EnumWindowParams*>(param);
	bool hwnd_match = (QWORD) hwnd == p->find_hwnd;
	bool hwnd_must_match = p->find_hwnd_must_match; // capture type specify instance only TODO

	if (!hwnd_match && hwnd_must_match) { 
		return TRUE;
	}

	if (!IsWindowVisible(hwnd)) {
		return TRUE;
	}

	HWND hwnd_try = GetAncestor(hwnd, GA_ROOTOWNER);
	HWND hwnd_walk = NULL;
	while (hwnd_try != hwnd_walk) {
		hwnd_walk = hwnd_try;
		hwnd_try = GetLastActivePopup(hwnd_walk);
		if (IsWindowVisible(hwnd_try))
			break;
	}

	if (hwnd_walk != hwnd) {
		return TRUE;
	}

	TITLEBARINFO ti;
	// the following removes some task tray programs and "Program Manager"
	ti.cbSize = sizeof(ti);
	GetTitleBarInfo(hwnd, &ti);
	if (ti.rgstate[0] & STATE_SYSTEM_INVISIBLE && !(ti.rgstate[0] & STATE_SYSTEM_FOCUSABLE)) {
		return TRUE;
	}

	// Tool windows should not be displayed either, these do not appear in the
	// task bar.
	if (GetWindowLong(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) {
		return TRUE;
	}

	const int buf_len = 1024;

	TCHAR class_name[buf_len] = { 0 };
	GetClassName(hwnd, class_name, buf_len);

	// check if class match
	bool class_match = lstrcmp(class_name, p->find_class_name) == 0;


	// check exe match
	bool exe_match = false;
	DWORD pid;
	GetWindowThreadProcessId(hwnd, &pid);
	HANDLE handle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, false, pid);
	TCHAR exe_name[buf_len] = { 0 };

	if (handle != NULL) {
		GetModuleFileNameEx(handle, NULL, exe_name, buf_len);
		exe_match = lstrcmp(exe_name, p->find_exe_name) == 0;
		CloseHandle(handle);
	}

	bool found = exe_match && class_match;
	if (found) {
		p->to_capture_hwnd = hwnd;
		p->to_window_found = true;
	}

	// debug("WindowsProcVerifier. HWND: %lld, class name: %S, exe name: %S, found: %d", hwnd, class_name, exe_name, found);

	return !found;
}

HWND CPushPinDesktop::FindCaptureWindows(bool hwnd_must_match, QWORD capture_handle, LPWSTR capture_class, LPWSTR capture_name, LPWSTR capture_exe_name) {
	EnumWindowParams cb;
	cb.find_hwnd = capture_handle;
	cb.find_class_name = capture_class;
	cb.find_window_name = capture_name;
	cb.find_exe_name = capture_exe_name;
	cb.find_hwnd_must_match = hwnd_must_match;
	cb.to_window_found = false;
	cb.to_capture_hwnd = NULL;

	EnumWindows(&WindowsProcVerifier, reinterpret_cast<LPARAM>(&cb));
	return cb.to_capture_hwnd;
}
