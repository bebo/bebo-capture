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
#define EVENT_READ_REGISTRY "Global\\BEBO_CAPTURE_READ_REGISTRY"

extern "C" {
	extern bool load_graphics_offsets(bool is32bit);
}

DWORD globalStart; // for some debug performance benchmarking
long countMissed = 0;
long fastestRoundMillis = 1000000; // random big number
long sumMillisTook = 0;

char out[1024];
// FIXME :  move these
bool ever_started = false;
boolean missed = false;

#ifdef _DEBUG 
int show_performance = 1;
#else
int show_performance = 0;
#endif

volatile bool initialized = false;

static DWORD WINAPI init_hooks(LPVOID unused)
{
	info("Init hooks: load graphics offsets start");
	bool x32 = load_graphics_offsets(true);
	bool x64 = load_graphics_offsets(false);
	info("Init hooks: load graphics offsets complete - is32bit: %d, is64bit: %d", x32, x64);
	return 0;
}

// the default child constructor...
CPushPinDesktop::CPushPinDesktop(HRESULT *phr, CGameCapture *pFilter)
	: CSourceStream(NAME("Push Source CPushPinDesktop child/pin"), phr, pFilter, L"Capture"),
	m_iFrameNumber(0),
	m_pParent(pFilter),
	m_bFormatAlreadySet(false),
	previousFrame(0),
	active(false),
	m_iCaptureType(-1),
	m_pCaptureTypeName(L""),
	m_pCaptureLabel(L""),
	m_pCaptureId(L""),
	m_pCaptureWindowName(NULL),
	m_pCaptureWindowClassName(NULL),
	m_pCaptureExeFullName(NULL),
	game_context(NULL),
	m_pDesktopCapture(new DesktopCapture),
	m_pGDICapture(new GDICapture),
	m_iDesktopNumber(-1),
	m_iDesktopAdapterNumber(-1),
	m_iCaptureHandle(-1),
	m_bCaptureOnce(0),
	m_iCaptureConfigWidth(0),
	m_iCaptureConfigHeight(0),
	m_rtFrameLength(UNITS / 30),
	readRegistryEvent(NULL),
	init_hooks_thread(NULL),
	threadCreated(false),
	isBlackFrame(true)
{
	info("CPushPinDesktop");

	switch (m_iCaptureType) {
	case CAPTURE_INJECT:
		registry.Open(HKEY_CURRENT_USER, L"Software\\Bebo\\GameCapture", KEY_READ);
		break;
	case CAPTURE_DESKTOP:
		registry.Open(HKEY_CURRENT_USER, L"Software\\Bebo\\DesktopCapture", KEY_READ);
		break;
	case CAPTURE_GDI:
		registry.Open(HKEY_CURRENT_USER, L"Software\\Bebo\\GdiCapture", KEY_READ);
		break;
	default:
		registry.Open(HKEY_CURRENT_USER, L"Software\\Bebo\\GameCapture", KEY_READ);
		break;
	}

	// Get the device context of the main display, just to get some metrics for it...
	config = (struct game_capture_config*) malloc(sizeof game_capture_config);
	memset(config, 0, sizeof game_capture_config);

	if (!initialized) {
		init_hooks_thread = CreateThread(NULL, 0, init_hooks, NULL, 0, NULL);
		DWORD result = WaitForSingleObject(init_hooks_thread, INFINITE);
		initialized = true;
		info("Init hooks result: 0x%08x (%d)", result, result);	
	}

	if (!readRegistryEvent) {
		readRegistryEvent = CreateEvent(NULL,
			TRUE,
			FALSE,
			TEXT(EVENT_READ_REGISTRY));

		if (readRegistryEvent == NULL) {
			warn("Failed to create read registry signal event. Attempting to open event.");
			readRegistryEvent = OpenEvent(EVENT_ALL_ACCESS,
				FALSE,
				TEXT(EVENT_READ_REGISTRY));

			if (readRegistryEvent == NULL) {
				error("Failed to open registry signal event, after attempted to create it. We should die here.");
			}
		} else {
			info("Created read registry signal event. Handle: %lld", readRegistryEvent);
		}
	}

	// now read some custom settings...
	WarmupCounter();

	GetGameFromRegistry();
}

CPushPinDesktop::~CPushPinDesktop()
{
	if (game_context) {
		stop_game_capture(&game_context);
		game_context = NULL;
	}
	
	if (m_pDesktopCapture) {
		delete m_pDesktopCapture;
		m_pDesktopCapture = nullptr;
	}

	if (m_pGDICapture) {
		delete m_pGDICapture;
		m_pGDICapture = nullptr;
	}

	if (readRegistryEvent) {
		CloseHandle(readRegistryEvent);
	}

	CleanupCapture();
}

void CPushPinDesktop::CleanupCapture() {
	if (game_context) {
		stop_game_capture(&game_context);
		game_context = NULL;
	}

	if (m_pDesktopCapture) {
		m_pDesktopCapture->Cleanup();
	}

	if (m_pGDICapture) {
		m_pGDICapture->SetCaptureHandle(NULL);
	}

	if (!threadCreated) {
		LOG(INFO) << "Total no. Frames written: " << m_iFrameNumber << ", before thread created.";
	} else {
		LOG(INFO) << "Total no. Frames written: " << m_iFrameNumber << " " << out;
	}

	// reset counter values 

	globalStart = GetTickCount();
	missed = true;
	countMissed = 0;
	sumMillisTook = 0;
	fastestRoundMillis = LONG_MAX;
	m_iFrameNumber = 0;
	previousFrame = 0;
	isBlackFrame = true;

	sprintf(out, "done video frame! total frames: %d this one %dx%d -> (%dx%d) took: %.02Lfms, %.02f ave fps (%.02f is the theoretical max fps based on this round, ave. possible fps %.02f, fastest round fps %.02f, negotiated fps %.06f), frame missed: %d, type: %S, label: %S, id: %S, black frame: %d",
		m_iFrameNumber, m_iCaptureConfigWidth, m_iCaptureConfigHeight, getNegotiatedFinalWidth(), getNegotiatedFinalHeight(), 
		0, 0, 0, 0, 0, 0, countMissed, 
		m_pCaptureTypeName.c_str(), m_pCaptureLabel.c_str(), m_pCaptureId.c_str(), isBlackFrame);
}

void CPushPinDesktop::LogCapture() {
	info("Capture Type: %S, Capture Label: %S, Capture Id: %S", m_pCaptureTypeName.c_str(), m_pCaptureLabel.c_str(), m_pCaptureId.c_str()); 
}

int CPushPinDesktop::GetGameFromRegistry(void) {
	std::wstringstream message;
	message << "Reading from registry: ";

	int numberOfChanges = 0;

	if (registry.HasValue(TEXT("CaptureType"))) {
		std::wstring data;
		registry.ReadValue(TEXT("CaptureType"), &data);

		int newCaptureType = -1;
		char type[1024];
		sprintf(type, "%S", data.c_str());

		if (strcmp(type, "desktop") == 0) {
			newCaptureType = CAPTURE_DESKTOP;
		}
		else if (strcmp(type, "inject") == 0) {
			newCaptureType = CAPTURE_INJECT;
		}
		else if (strcmp(type, "gdi") == 0) {
			newCaptureType = CAPTURE_GDI;
		}
		else if (strcmp(type, "dshow") == 0) {
			newCaptureType = CAPTURE_DSHOW;
		}
		if (newCaptureType > -1 && m_iCaptureType != newCaptureType) {
			m_iCaptureType = newCaptureType;
			m_pCaptureTypeName = data;
			message << "CaptureType: " << type << " (" << m_iCaptureType << "), ";
			numberOfChanges++;
		}
	}

	if (registry.HasValue(TEXT("CaptureId"))) {
		std::wstring data;
		registry.ReadValue(TEXT("CaptureId"), &data);

		char text[1024];
		sprintf(text, "%S", data.c_str());

		int oldAdapterId = m_iDesktopAdapterNumber;
		int oldDesktopId = m_iDesktopNumber;
		int newAdapterId = m_iDesktopAdapterNumber;
		int newDesktopId = m_iDesktopNumber;

		char * typeName = strtok(text, ":");
		char * adapterId = strtok(NULL, ":");
		char * desktopId = strtok(NULL, ":");

		if (adapterId != NULL) {
			if (strcmp(typeName, "desktop") == 0) {
				newAdapterId = atoi(adapterId);
			}
		}

		if (desktopId != NULL) {
			if (strcmp(typeName, "desktop") == 0) {
				newDesktopId = atoi(desktopId);
			}
		}

		if (oldAdapterId != newAdapterId ||
			oldDesktopId != newDesktopId) {
			m_iDesktopAdapterNumber = newAdapterId;
			m_iDesktopNumber = newDesktopId;
			m_pCaptureId = data;
			message << "CaptureId: " << typeName << ":" << m_iDesktopAdapterNumber << ":" << m_iDesktopNumber << ", ";
			numberOfChanges++;
		}
	}

	if (registry.HasValue(TEXT("CaptureWindowName"))) {
		std::wstring data;

		registry.ReadValue(TEXT("CaptureWindowName"), &data);

		if (m_pCaptureWindowName == NULL || wcscmp(data.c_str(), m_pCaptureWindowName) != 0) {
			if (m_pCaptureWindowName != NULL) {
				delete[] m_pCaptureWindowName;
			}
			m_pCaptureWindowName = wcsdup(data.c_str());
			message << "CaptureWindowName: " << m_pCaptureWindowName << ", ";
			numberOfChanges++;
		}
	}

	if (registry.HasValue(TEXT("CaptureWindowClassName"))) {
		std::wstring data;

		registry.ReadValue(TEXT("CaptureWindowClassName"), &data);

		if (m_pCaptureWindowClassName == NULL || wcscmp(data.c_str(), m_pCaptureWindowClassName) != 0) {
			if (m_pCaptureWindowClassName != NULL) {
				delete[] m_pCaptureWindowClassName;
			}
			m_pCaptureWindowClassName = wcsdup(data.c_str());
			message << "CaptureWindowClassName: " << m_pCaptureWindowClassName << ", ";
			numberOfChanges++;
		}
	}

	if (registry.HasValue(TEXT("CaptureExeFullName"))) {
		std::wstring data;

		registry.ReadValue(TEXT("CaptureExeFullName"), &data);

		if (m_pCaptureExeFullName == NULL || wcscmp(data.c_str(), m_pCaptureExeFullName) != 0) {
			if (m_pCaptureExeFullName != NULL) {
				delete[] m_pCaptureExeFullName;
			}
			m_pCaptureExeFullName = wcsdup(data.c_str());
			message << "CaptureExeFullName: " << m_pCaptureExeFullName << ", ";
			numberOfChanges++;
		}
	}

	if (registry.HasValue(TEXT("CaptureLabel"))) {
		std::wstring data;

		registry.ReadValue(TEXT("CaptureLabel"), &data);

		if (data.compare(m_pCaptureLabel) != 0) {
			m_pCaptureLabel = data;
			message << "CaptureLabel: " << m_pCaptureLabel << ", ";
			numberOfChanges++;
		}
	}

	if (registry.HasValue(TEXT("CaptureWindowHandle"))) {
		int64_t qout;

		registry.ReadInt64(TEXT("CaptureWindowHandle"), &qout);

		if (m_iCaptureHandle != qout) {
			m_iCaptureHandle = qout;
			message << "CaptureWindowHandle: " << m_iCaptureHandle << ", ";
			numberOfChanges++;
		}
	}

	if (registry.HasValue(TEXT("CaptureAntiCheat"))) {
		DWORD qout;

		registry.ReadValueDW(TEXT("CaptureAntiCheat"), &qout);
		if (m_bCaptureAntiCheat != qout) {
			m_bCaptureAntiCheat = (qout == 1);
			message << "CaptureAntiCheat: " << m_bCaptureAntiCheat << ", ";
			numberOfChanges++;
		}
	}

	if (registry.HasValue(TEXT("CaptureOnce"))) {
		DWORD qout;

		registry.ReadValueDW(TEXT("CaptureOnce"), &qout);

		if (m_bCaptureOnce != qout) {
			m_bCaptureOnce = (qout == 1);
			message << "CaptureOnce: " << m_bCaptureOnce << ", ";
			numberOfChanges++;
		}
	}

	if (registry.HasValue(TEXT("CaptureFPS"))) {
		DWORD oldCaptureFPS = GetFps();
		DWORD newCaptureFPS = -1;

		registry.ReadValueDW(TEXT("CaptureFPS"), &newCaptureFPS);

		if (oldCaptureFPS != newCaptureFPS) {
			m_rtFrameLength = UNITS / newCaptureFPS;
			message << "CaptureFPS: %d" << newCaptureFPS << ", ";
			numberOfChanges++;
		}
	}

	if (numberOfChanges > 0) {
		std::wstring wstr = message.str();
		wstr.erase(wstr.size() - 2);
		info("%S", wstr.c_str());
	}

	return numberOfChanges;
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

	if (!m_pDesktopCapture->IsReady()) {
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
	}

	if (!m_pDesktopCapture->IsReady()) {
		return 2; // skip the loop
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

	if (m_iFrameNumber == 1) {
		info("Got first frame, type: %S, label: %S", m_pCaptureTypeName.c_str(), m_pCaptureId.c_str());
	}

	if (m_iFrameNumber <= 10 && isBlackFrame) {
		long size = pSample->GetSize();
		long y_size = m_iCaptureConfigWidth * m_iCaptureConfigHeight;
		BYTE* pData;
		pSample->GetPointer(&pData);
		for (int i = 0; i < size; i++) {
			if ((i < y_size && pData[i] != 0x10) || (i >= y_size && pData[i] != 0x80)) {
				 isBlackFrame = false;
				 break;
			}
		}

		if (m_iFrameNumber == 10 && isBlackFrame) {
			error("Black frame detected, type: %S, label: %S", m_pCaptureTypeName.c_str(), m_pCaptureId.c_str());
		}
	}	

	// Set TRUE on every sample for uncompressed frames http://msdn.microsoft.com/en-us/library/windows/desktop/dd407021%28v=vs.85%29.aspx
	pSample->SetSyncPoint(TRUE);

	// only set discontinuous for the first...I think...
	pSample->SetDiscontinuity(m_iFrameNumber <= 1);

	double m_fFpsSinceBeginningOfTime = ((double)m_iFrameNumber) / (GetTickCount() - globalStart) * 1000;
	sprintf(out, "done video frame! total frames: %d this one %dx%d -> (%dx%d) took: %.02Lfms, %.02f ave fps (%.02f is the theoretical max fps based on this round, ave. possible fps %.02f, fastest round fps %.02f, negotiated fps %.06f), frame missed: %d, type: %S, label: %S, id: %S, black frame: %d",
		m_iFrameNumber, m_iCaptureConfigWidth, m_iCaptureConfigHeight, getNegotiatedFinalWidth(), getNegotiatedFinalHeight(), millisThisRoundTook, m_fFpsSinceBeginningOfTime, 1.0 * 1000 / millisThisRoundTook,
		/* average */ 1.0 * 1000 * m_iFrameNumber / sumMillisTook, 1.0 * 1000 / fastestRoundMillis, GetFps(), countMissed, m_pCaptureTypeName.c_str(), m_pCaptureLabel.c_str(), m_pCaptureId.c_str(), isBlackFrame);
	debug(out);
	return S_OK;
}

void CPushPinDesktop::ProcessRegistryReadEvent(long timeout) {
	DWORD result = WaitForSingleObject(readRegistryEvent, timeout);
	if (result == WAIT_OBJECT_0) {
		int changeCount = GetGameFromRegistry();

		if (changeCount > 0) {
		    info("Received re-read registry event, number of changes in registry: %d", changeCount);
			CleanupCapture();
		}

		ResetEvent(readRegistryEvent);
	}
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
		ProcessRegistryReadEvent(0);

		switch (m_iCaptureType) {
		case CAPTURE_INJECT:
			break;
		case CAPTURE_DESKTOP: {
			int code = FillBuffer_Desktop(pSample);
			if (code == 2) { 
				// failed to initialize desktop capture, sleep is to reduce the # of log that we failed
				// this failure can happen pretty often due to mobile graphic card
				// we should detect + log this smartly instead of putting 3s sleep hack.
				ProcessRegistryReadEvent(3000);
				continue; 
			}
			return code;
		}
		case CAPTURE_GDI: {
			int code = FillBuffer_GDI(pSample);
			if (code == 2) {
				ProcessRegistryReadEvent(300);
				continue; // gdi failed to get frame - try go to next loop
			}
			return code;
		}
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
				ProcessRegistryReadEvent(300);
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

	if (m_iFrameNumber == 1) {
		info("Got first frame, type: %S, label: %S", m_pCaptureTypeName.c_str(), m_pCaptureLabel.c_str());
	}

	if (m_iFrameNumber <= 10 && isBlackFrame) {
		long size = pSample->GetSize();
		long y_size = m_iCaptureConfigWidth * m_iCaptureConfigHeight;
		BYTE* pData;
		pSample->GetPointer(&pData);
		for (int i = 0; i < size; i++) {
			if ((i < y_size && pData[i] != 0x10) || (i >= y_size && pData[i] != 0x80)) {
				 isBlackFrame = false;
				 break;
			}
		}

		if (m_iFrameNumber == 10 && isBlackFrame) {
			error("Black frame detected, type: %S, label: %S", m_pCaptureTypeName.c_str(), m_pCaptureLabel.c_str());
		}
	}	

	// Set TRUE on every sample for uncompressed frames http://msdn.microsoft.com/en-us/library/windows/desktop/dd407021%28v=vs.85%29.aspx
	pSample->SetSyncPoint(TRUE);

	// only set discontinuous for the first...I think...
	pSample->SetDiscontinuity(m_iFrameNumber <= 1);

	double m_fFpsSinceBeginningOfTime = ((double)m_iFrameNumber) / (GetTickCount() - globalStart) * 1000;
	sprintf(out, "done video frame! total frames: %d this one %dx%d -> (%dx%d) took: %.02Lfms, %.02f ave fps (%.02f is the theoretical max fps based on this round, ave. possible fps %.02f, fastest round fps %.02f, negotiated fps %.06f), frame missed: %d, type: %S, label: %S, id: %S, black frame: %d",
		m_iFrameNumber, m_iCaptureConfigWidth, m_iCaptureConfigHeight, getNegotiatedFinalWidth(), getNegotiatedFinalHeight(), millisThisRoundTook, m_fFpsSinceBeginningOfTime, 1.0 * 1000 / millisThisRoundTook,
		/* average */ 1.0 * 1000 * m_iFrameNumber / sumMillisTook, 1.0 * 1000 / fastestRoundMillis, GetFps(), countMissed, m_pCaptureTypeName.c_str(), m_pCaptureLabel.c_str(), m_pCaptureId.c_str(), isBlackFrame);
	debug(out);
	return S_OK;
}

HRESULT CPushPinDesktop::FillBuffer_GDI(IMediaSample *pSample)
{
	CheckPointer(pSample, E_POINTER);

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

		if (!m_pGDICapture->IsReady()) {
			HWND hwnd = FindCaptureWindows(m_bCaptureOnce, m_iCaptureHandle, m_pCaptureWindowClassName, m_pCaptureWindowName, m_pCaptureExeFullName);

			if (!hwnd) {
				return 2;
			}

			info("GDI - window_handle: 0x%016x (%ld), class_name: %S, window_name: %S, exe_name: %S, capture_once: %d",
				m_iCaptureHandle, m_iCaptureHandle, m_pCaptureWindowClassName, m_pCaptureWindowName, m_pCaptureExeFullName, m_bCaptureOnce);

			m_pGDICapture->SetSize(getNegotiatedFinalWidth(), getNegotiatedFinalHeight());
			m_pGDICapture->SetCaptureHandle(hwnd);

			globalStart = GetTickCount();
			countMissed = 0;
			sumMillisTook = 0;
			fastestRoundMillis = LONG_MAX;
			m_iFrameNumber = 0;
			missed = true;
			previousFrame = 0;
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
			if (!IsWindow(m_pGDICapture->GetCaptureHandle())) {
				info("capturing window is no longer alive"); // TODO: instead of dying - maybe retrying
				m_pGDICapture->SetCaptureHandle(NULL);
			}
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

	if (m_iFrameNumber == 1) {
		info("Got first frame, type: %S, label: %S", m_pCaptureTypeName.c_str(), m_pCaptureLabel.c_str());
	}

	if (m_iFrameNumber <= 10 && isBlackFrame) {
		long size = pSample->GetSize();
		long y_size = m_iCaptureConfigWidth * m_iCaptureConfigHeight;
		BYTE* pData;
		pSample->GetPointer(&pData);
		for (int i = 0; i < size; i++) {
			if ((i < y_size && pData[i] != 0x10) || (i >= y_size && pData[i] != 0x80)) {
				 isBlackFrame = false;
				 break;
			}
		}

		if (m_iFrameNumber == 10 && isBlackFrame) {
			error("Black frame detected, type: %S, label: %S", m_pCaptureTypeName.c_str(), m_pCaptureLabel.c_str());
		}
	}	

	// Set TRUE on every sample for uncompressed frames http://msdn.microsoft.com/en-us/library/windows/desktop/dd407021%28v=vs.85%29.aspx
	pSample->SetSyncPoint(TRUE);

	// only set discontinuous for the first...I think...
	pSample->SetDiscontinuity(m_iFrameNumber <= 1);

	double m_fFpsSinceBeginningOfTime = ((double)m_iFrameNumber) / (GetTickCount() - globalStart) * 1000;
	sprintf(out, "done video frame! total frames: %d this one %dx%d -> (%dx%d) took: %.02Lfms, %.02f ave fps (%.02f is the theoretical max fps based on this round, ave. possible fps %.02f, fastest round fps %.02f, negotiated fps %.06f), frame missed: %d, type: %S, label: %S, id: %S, black frame: %d",
		m_iFrameNumber, m_iCaptureConfigWidth, m_iCaptureConfigHeight, getNegotiatedFinalWidth(), getNegotiatedFinalHeight(), millisThisRoundTook, m_fFpsSinceBeginningOfTime, 1.0 * 1000 / millisThisRoundTook,
		/* average */ 1.0 * 1000 * m_iFrameNumber / sumMillisTook, 1.0 * 1000 / fastestRoundMillis, GetFps(), countMissed, m_pCaptureTypeName.c_str(), m_pCaptureLabel.c_str(), m_pCaptureId.c_str(), isBlackFrame);
	debug(out);
	return S_OK;
}

float CPushPinDesktop::GetFps() {
	return (float)(UNITS / m_rtFrameLength);
}

int CPushPinDesktop::getNegotiatedFinalWidth() {
	return m_iCaptureConfigWidth;
}

int CPushPinDesktop::getNegotiatedFinalHeight() {
	return m_iCaptureConfigHeight;
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
	int bytesPerPixel = 32 / 8; // we convert from a 32 bit to i420, so need more space in this case

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
	pProperties->cbBuffer = header.biHeight * header.biWidth * 3 / 2; // necessary to prevent an "out of memory" error for FMLE. Yikes. Oh wow yikes.

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

	previousFrame = 0; // reset
	m_iFrameNumber = 0;

	return NOERROR;
} // DecideBufferSize


HRESULT CPushPinDesktop::OnThreadCreate() {
	info("CPushPinDesktop OnThreadCreate");
	previousFrame = 0; // reset <sigh> dunno if this helps FME which sometimes had inconsistencies, or not
	m_iFrameNumber = 0;
	threadCreated = true;
	return S_OK;
}

HRESULT CPushPinDesktop::OnThreadDestroy() {
	info("CPushPinDesktop::OnThreadDestroy");
	CleanupCapture();
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

	if (p->find_exe_name && wcslen(p->find_exe_name) > 0) {
		DWORD pid;
		GetWindowThreadProcessId(hwnd, &pid);
		HANDLE handle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, false, pid);
		TCHAR exe_name[buf_len] = { 0 };

		if (handle != NULL) {
			GetModuleFileNameEx(handle, NULL, exe_name, buf_len);
			exe_match = lstrcmp(exe_name, p->find_exe_name) == 0;
			CloseHandle(handle);
		}
	} else {
		exe_match = true;
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
