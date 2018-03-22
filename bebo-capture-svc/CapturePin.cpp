#include <streams.h>

#include <tchar.h>
#include "Capture.h"
#include "names_and_ids.h"
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

wchar_t out[1024];
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

static const std::wstring GetTypeName(int type) {
	switch (type) {
	case CAPTURE_INJECT: return L"inject";
	case CAPTURE_GDI: return L"gdi";
	case CAPTURE_DESKTOP: return L"desktop";
	default: return L"";
	}
}

// the default child constructor...
CPushPinDesktop::CPushPinDesktop(HRESULT *phr, CGameCapture *pFilter, int capture_type)
	: CSourceStream(NAME("Push Source CPushPinDesktop child/pin"), phr, pFilter, L"Capture"),
	m_iFrameNumber(0),
	m_pParent(pFilter),
	m_bFormatAlreadySet(false),
	previousFrame(0),
	active(false),
	type_(capture_type),
	typeName_(GetTypeName(capture_type)),
	label_(L""),
	id_(L""),
	windowName_(NULL),
	windowClassName_(NULL),
	exeFullName_(NULL),
	game_context(NULL),
	m_pDesktopCapture(new DesktopCapture),
	m_pGDICapture(new GDICapture),
	m_iDesktopNumber(-1),
	m_iDesktopAdapterNumber(-1),
	windowHandle_(-1),
	once_(0),
	width_(0),
	height_(0),
	m_rtFrameLength(UNITS / 30),
	readRegistryEvent(NULL),
	init_hooks_thread(NULL),
	threadCreated(false),
	isBlackFrame(true),
	blackFrameCount(0)
{

	info("CPushPinDesktop capture_type: %d", capture_type);

	switch (type_) {
	case CAPTURE_INJECT:
		registry.Open(HKEY_CURRENT_USER, L"Software\\Bebo\\GameCapture", KEY_READ);
		break;
	case CAPTURE_DESKTOP:
		registry.Open(HKEY_CURRENT_USER, L"Software\\Bebo\\DesktopCapture", KEY_READ);
		break;
	case CAPTURE_GDI:
		registry.Open(HKEY_CURRENT_USER, L"Software\\Bebo\\WindowCapture", KEY_READ);
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
		readRegistryEvent = OpenEvent(EVENT_ALL_ACCESS,
			FALSE,
			TEXT(EVENT_READ_REGISTRY));
		
		if (readRegistryEvent == NULL) {
			readRegistryEvent = CreateEvent(NULL,
				TRUE,
				FALSE,
				TEXT(EVENT_READ_REGISTRY));

			if (readRegistryEvent == NULL) {
				error("Failed to create registry signal event, after attempted to open it first. We should die here.");
			}
		}

		info("Read registry signal event. Handle: %llu", readRegistryEvent);
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
		char out_c[1024] = { 0 };
		WideCharToMultiByte(CP_UTF8, 0, out, 1024, out_c, 1024, NULL, NULL);
		LOG(INFO) << "Total no. Frames written: " << m_iFrameNumber << " " << out_c;
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
	blackFrameCount = 0;

	_swprintf(out, L"done video frame! total frames: %d this one %dx%d -> (%dx%d) took: %.02Lfms, %.02f ave fps (%.02f is the theoretical max fps based on this round, ave. possible fps %.02f, fastest round fps %.02f, negotiated fps %.06f), frame missed: %d, type: %ls, name: %ls, black frame: %d, black frame count: %llu",
		m_iFrameNumber, width_, height_, getNegotiatedFinalWidth(), getNegotiatedFinalHeight(), 
		0, 0, 0, 0, 0, 0, countMissed, 
		typeName_.c_str(), label_.c_str(), isBlackFrame, blackFrameCount);
}

int CPushPinDesktop::GetGameFromRegistry(void) {
	std::wstringstream message;
	message << "Reading from registry: ";

	int numberOfChanges = 0;

#if 0
	if (registry.HasValue(TEXT("type"))) {
		std::wstring data;
		registry.ReadValue(TEXT("type"), &data);

		int newtype = -1;
		wchar_t type[1024];
		_swprintf(type, L"%ls", data.c_str());

		if (wcscmp(type, L"desktop") == 0) {
			newtype = CAPTURE_DESKTOP;
		}
		else if (wcscmp(type, L"inject") == 0) {
			newtype = CAPTURE_INJECT;
		}
		else if (wcscmp(type, L"gdi") == 0) {
			newtype = CAPTURE_GDI;
		}
		else if (wcscmp(type, L"dshow") == 0) {
			newtype = CAPTURE_DSHOW;
		}
		if (newtype > -1 && m_itype != newtype) {
			m_itype = newtype;
			m_ptypeName = data;
			message << "type: " << type << " (" << m_itype << "), ";
			numberOfChanges++;
		}
	}
#endif

	if (registry.HasValue(TEXT("id"))) {
		std::wstring data;
		registry.ReadValue(TEXT("id"), &data);

		wchar_t text[1024];
		_swprintf(text, L"%ls", data.c_str());

		int oldAdapterId = m_iDesktopAdapterNumber;
		int oldDesktopId = m_iDesktopNumber;
		int newAdapterId = m_iDesktopAdapterNumber;
		int newDesktopId = m_iDesktopNumber;

		wchar_t * typeName = _wcstok(text, L":");
		wchar_t * adapterId = _wcstok(NULL, L":");
		wchar_t * desktopId = _wcstok(NULL, L":");

		if (adapterId != NULL) {
			if (wcscmp(typeName, L"desktop") == 0) {
				newAdapterId = _wtoi(adapterId);
			}
		}

		if (desktopId != NULL) {
			if (wcscmp(typeName, L"desktop") == 0) {
				newDesktopId = _wtoi(desktopId);
			}
		}

		if (oldAdapterId != newAdapterId ||
			oldDesktopId != newDesktopId) {
			m_iDesktopAdapterNumber = newAdapterId;
			m_iDesktopNumber = newDesktopId;
			id_ = data;
			message << "id: " << typeName << ":" << m_iDesktopAdapterNumber << ":" << m_iDesktopNumber << ", ";
			numberOfChanges++;
		}
	}

	if (registry.HasValue(TEXT("windowName"))) {
		std::wstring data;

		registry.ReadValue(TEXT("windowName"), &data);

		if (windowName_ == NULL || wcscmp(data.c_str(), windowName_) != 0) {
			if (windowName_ != NULL) {
				delete[] windowName_;
			}
			windowName_ = _wcsdup(data.c_str());
			message << "windowName: " << windowName_ << ", ";
			numberOfChanges++;
		}
	}

	if (registry.HasValue(TEXT("windowClassName"))) {
		std::wstring data;

		registry.ReadValue(TEXT("windowClassName"), &data);

		if (windowClassName_ == NULL || wcscmp(data.c_str(), windowClassName_) != 0) {
			if (windowClassName_ != NULL) {
				delete[] windowClassName_;
			}
			windowClassName_ = _wcsdup(data.c_str());
			message << "windowClassName: " << windowClassName_ << ", ";
			numberOfChanges++;
		}
	}

	if (registry.HasValue(TEXT("exeFullName"))) {
		std::wstring data;

		registry.ReadValue(TEXT("exeFullName"), &data);

		if (exeFullName_ == NULL || wcscmp(data.c_str(), exeFullName_) != 0) {
			if (exeFullName_ != NULL) {
				delete[] exeFullName_;
			}
			exeFullName_ = _wcsdup(data.c_str());
			message << "exeFullName: " << exeFullName_ << ", ";
			numberOfChanges++;
		}
	}

	if (registry.HasValue(TEXT("label"))) {
		std::wstring data;

		registry.ReadValue(TEXT("label"), &data);

		if (type_ == CAPTURE_DESKTOP && data.length() == 0) {
			if (label_.compare(id_) != 0) {
				label_ = id_;
				message << "label: " << label_ << ", ";
				numberOfChanges++;
			}
		} else if (data.compare(label_) != 0) {
			label_ = data;
			message << "label: " << label_ << ", ";
			numberOfChanges++;
		}
	}

	if (registry.HasValue(TEXT("windowHandle"))) {
		int64_t qout;

		registry.ReadInt64(TEXT("windowHandle"), &qout);

		if (windowHandle_ != qout) {
			windowHandle_ = qout;
			message << "windowHandle: " << windowHandle_ << ", ";
			numberOfChanges++;
		}
	}

	if (registry.HasValue(TEXT("antiCheat"))) {
		DWORD qout;

		registry.ReadValueDW(TEXT("antiCheat"), &qout);
		if (antiCheat_ != qout) {
			antiCheat_ = (qout == 1);
			message << "antiCheat: " << antiCheat_ << ", ";
			numberOfChanges++;
		}
	}

	if (registry.HasValue(TEXT("once"))) {
		DWORD qout;

		registry.ReadValueDW(TEXT("once"), &qout);

		if (once_ != qout) {
			once_ = (qout == 1);
			message << "once: " << once_ << ", ";
			numberOfChanges++;
		}
	}

	if (registry.HasValue(TEXT("CaptureFPS"))) {
		DWORD oldfps = GetFps();
		DWORD newfps = -1;

		registry.ReadValueDW(TEXT("CaptureFPS"), &newfps);

		if (oldfps != newfps) {
			m_rtFrameLength = UNITS / newfps;
			message << "fps: " << newfps << ", ";
			numberOfChanges++;
		}
	}

	if (numberOfChanges > 0) {
		std::wstring wstr = message.str();
		wstr.erase(wstr.size() - 2);
		info("%ls", wstr.c_str());
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
		if (!active) {
			info("FillBuffer - inactive");
			return S_FALSE;
		}

		ProcessRegistryReadEvent(0);

		int code = E_FAIL;

		switch (type_) {
		case CAPTURE_INJECT:
			code = FillBuffer_Inject(pSample);
			break;
		case CAPTURE_DESKTOP: 
			code = FillBuffer_Desktop(pSample);
			break;
		case CAPTURE_GDI: 
			code = FillBuffer_GDI(pSample);
			break;
		case CAPTURE_DSHOW:
			error("LIBDSHOW CAPTURE IS NOT SUPPRTED YET");
			break;
		default:
			error("UNKNOWN CAPTURE TYPE: %d", type_);
		}			

		// failed to initialize desktop capture, sleep is to reduce the # of log that we failed
		// this failure can happen pretty often due to mobile graphic card
		// we should detect + log this smartly instead of putting 3s sleep hack.

		if (code == S_OK) {
			gotFrame = true;
		} else if (code == 2) { // not initialized yet
			gotFrame = false;
			long sleep = (type_ == CAPTURE_DESKTOP) ? 3000 : 300;
			ProcessRegistryReadEvent(sleep);
			continue;
		} else if (code == 3) { // black frame
			gotFrame = false;

			long double avg_fps = (sumMillisTook == 0) ? 0 : 1.0 * 1000 * m_iFrameNumber / sumMillisTook;
			long double max = (millisThisRoundTook == 0) ? 0 : 1.0 * 1000 / millisThisRoundTook;

			double m_fFpsSinceBeginningOfTime = ((double)m_iFrameNumber) / (GetTickCount() - globalStart) * 1000;

			_swprintf(out, L"done video frame! total frames: %d this one %dx%d -> (%dx%d) took: %.02Lfms, %.02f ave fps (%.02f is the theoretical max fps based on this round, ave. possible fps %.02f, fastest round fps %.02f, negotiated fps %.06f), frame missed: %d, type: %ls, name: %ls, black frame: %d, black frame count: %llu",
				m_iFrameNumber, width_, height_, 
				getNegotiatedFinalWidth(), getNegotiatedFinalHeight(), millisThisRoundTook, m_fFpsSinceBeginningOfTime, 
				max, sumMillisTook, 
				1.0 * 1000 / fastestRoundMillis, GetFps(), countMissed, typeName_.c_str(), label_.c_str(), isBlackFrame, blackFrameCount);
		} else {
			gotFrame = false;
			ProcessRegistryReadEvent(5000);
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

	if ((m_iFrameNumber - countMissed) == 1) {
		info("Got first frame, type: %ls, name: %ls", typeName_.c_str(), label_.c_str());
	}

	// Set TRUE on every sample for uncompressed frames http://msdn.microsoft.com/en-us/library/windows/desktop/dd407021%28v=vs.85%29.aspx
	pSample->SetSyncPoint(TRUE);

	// only set discontinuous for the first...I think...
	pSample->SetDiscontinuity(m_iFrameNumber <= 1);

	double m_fFpsSinceBeginningOfTime = ((double)m_iFrameNumber) / (GetTickCount() - globalStart) * 1000;
	_swprintf(out, L"done video frame! total frames: %d this one %dx%d -> (%dx%d) took: %.02Lfms, %.02f ave fps (%.02f is the theoretical max fps based on this round, ave. possible fps %.02f, fastest round fps %.02f, negotiated fps %.06f), frame missed: %d, type: %ls, name: %ls, black frame: %d, black frame count: %llu",
		m_iFrameNumber, width_, height_, getNegotiatedFinalWidth(), getNegotiatedFinalHeight(), millisThisRoundTook, m_fFpsSinceBeginningOfTime, 1.0 * 1000 / millisThisRoundTook,
		/* average */ 1.0 * 1000 * m_iFrameNumber / sumMillisTook, 1.0 * 1000 / fastestRoundMillis, GetFps(), countMissed, typeName_.c_str(), label_.c_str(), isBlackFrame, blackFrameCount);
	debug(out);
	return S_OK;
}

HRESULT CPushPinDesktop::FillBuffer_Inject(IMediaSample *pSample)
{
	CheckPointer(pSample, E_POINTER);
	
	if (!isReady(&game_context)) {
		if (m_iFrameNumber > 0) {
			CleanupCapture();
		}

		config->scale_cx = width_;
		config->scale_cy = height_;
		config->force_scaling = 1;
		config->anticheat_hook = antiCheat_;

		game_context = hook(&game_context, windowClassName_, windowName_, config, m_rtFrameLength * 100);

		if (!isReady(&game_context)) {
			return 2;
		}
	}

	CRefTime now;
	now = 0;
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

	bool frame = get_game_frame(&game_context, missed, pSample);
	if (!game_context) {
		frame = false;
		info("Capture Ended");
	}

	if (frame && previousFrame <= 0) {
		frame = false;
		previousFrame = now;
		missed = false;
		debug("skip first frame");
	}

	if (frame && isBlackFrame) {
		long size = pSample->GetSize();
		long y_size = width_ * height_;
		BYTE* pData;
		pSample->GetPointer(&pData);
		for (int i = 0; i < size; i++) {
			if ((i < y_size && pData[i] != 0x10) || (i >= y_size && pData[i] != 0x80)) {
				isBlackFrame = false;
				break;
			}
		}

		if (isBlackFrame) {
			frame = false;
			previousFrame = now;
			missed = false;
			blackFrameCount++;

			if (blackFrameCount == GetFps() * 10 * 2) { // 10s frames, cause we double sampling, texture A and B so 5*2
				error("Black frame detected, type: %ls, name: %ls", typeName_.c_str(), label_.c_str());
			}
		}
	}

	if (!frame) {
		return 3;
	}

	return S_OK;
}

HRESULT CPushPinDesktop::FillBuffer_Desktop(IMediaSample *pSample) {
	CheckPointer(pSample, E_POINTER);

	if (!m_pDesktopCapture->IsReady()) {
		if (m_iFrameNumber > 0) {
			CleanupCapture();
		}

		info("Initializing desktop capture - adapter: %d, desktop: %d, size: %dx%d",
			m_iDesktopAdapterNumber, m_iDesktopNumber, getNegotiatedFinalWidth(), getNegotiatedFinalHeight());
		m_pDesktopCapture->Init(m_iDesktopAdapterNumber, m_iDesktopNumber, getNegotiatedFinalWidth(), getNegotiatedFinalHeight());

		if (!m_pDesktopCapture->IsReady()) {
			return 2;
		}
	}

	CRefTime now;
	now = 0;
	CSourceStream::m_pFilter->StreamTime(now);
	if (now <= 0) {
		DWORD dwMilliseconds = (DWORD)(m_rtFrameLength / 10000L);
		debug("no reference graph clock - sleeping %d", dwMilliseconds);
		Sleep(dwMilliseconds);
	}
	else if (now < (previousFrame + m_rtFrameLength)) {
		DWORD dwMilliseconds = (DWORD)max(1, min((previousFrame + m_rtFrameLength - now), m_rtFrameLength) / 10000L);
		debug("sleeping - %d", dwMilliseconds);
		Sleep(dwMilliseconds);
	}
	else if (missed) {
		DWORD dwMilliseconds = (DWORD)(m_rtFrameLength / 20000L);
		debug("starting/missed - sleeping %d", dwMilliseconds);
		Sleep(dwMilliseconds);
		CSourceStream::m_pFilter->StreamTime(now);
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

	bool frame = m_pDesktopCapture->GetFrame(pSample, false, now);

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

	if (frame && isBlackFrame) {
		long size = pSample->GetSize();
		long y_size = width_ * height_;
		BYTE* pData;
		pSample->GetPointer(&pData);
		for (int i = 0; i < size; i++) {
			if ((i < y_size && pData[i] != 0x10) || (i >= y_size && pData[i] != 0x80)) {
				isBlackFrame = false;
				break;
			}
		}

		if (isBlackFrame) {
			frame = false;
			previousFrame = now;
			missed = false;
			blackFrameCount++;

			if (blackFrameCount == GetFps() * 5) { // 5s frames
				error("Black frame detected, type: %ls, name: %ls", typeName_.c_str(), label_.c_str());
			}
		}
	}

	if (!frame) {
		return 3;
	}

	return S_OK;
}



HRESULT CPushPinDesktop::FillBuffer_GDI(IMediaSample *pSample)
{
	CheckPointer(pSample, E_POINTER);

	if (!m_pGDICapture->IsReady()) {
		if (m_iFrameNumber > 0) {
			CleanupCapture();
		}

		HWND hwnd = FindCaptureWindows(once_, windowHandle_, windowClassName_, windowName_, exeFullName_);

		if (!hwnd) {
			return 2;
		}

		info("GDI - window_handle: 0x%016x (%ld), class_name: %ls, window_name: %ls, exe_name: %ls, capture_once: %d",
			windowHandle_, windowHandle_, windowClassName_, windowName_, exeFullName_, once_);

		m_pGDICapture->SetSize(getNegotiatedFinalWidth(), getNegotiatedFinalHeight());
		m_pGDICapture->SetCaptureHandle(hwnd);
	}

	CRefTime now;
	now = 0;

	CSourceStream::m_pFilter->StreamTime(now);
	if (now <= 0) {
		DWORD dwMilliseconds = (DWORD)(m_rtFrameLength / 10000L);
		debug("no reference graph clock - sleeping %d", dwMilliseconds);
		Sleep(dwMilliseconds);
	}
	else if (now < (previousFrame + m_rtFrameLength)) {
		DWORD dwMilliseconds = (DWORD)max(1, min((previousFrame + m_rtFrameLength - now), m_rtFrameLength) / 10000L);
		debug("sleeping - %d", dwMilliseconds);
		Sleep(dwMilliseconds);
	}
	else if (missed) {
		DWORD dwMilliseconds = (DWORD)(m_rtFrameLength / 20000L);
		debug("starting/missed - sleeping %d", dwMilliseconds);
		Sleep(dwMilliseconds);
		CSourceStream::m_pFilter->StreamTime(now);
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

	bool frame = m_pGDICapture->GetFrame(pSample);

	if (frame && previousFrame <= 0) {
		frame = false;
		previousFrame = now;
		missed = false;
		debug("skip first frame");
	}

	if (frame && isBlackFrame) {
		long size = pSample->GetSize();
		long y_size = width_ * height_;
		BYTE* pData;
		pSample->GetPointer(&pData);
		for (int i = 0; i < size; i++) {
			if ((i < y_size && pData[i] != 0x10) || (i >= y_size && pData[i] != 0x80)) {
				isBlackFrame = false;
				break;
			}
		}

		if (isBlackFrame) {
			frame = false;
			missed = false;
			previousFrame = now;
			blackFrameCount++;

			if (blackFrameCount == GetFps() * 5) { // 5s frames
				error("Black frame detected, type: %ls, name: %ls", typeName_.c_str(), label_.c_str());
			}
		}
	}

	if (!frame) {
		if (!IsWindow(m_pGDICapture->GetCaptureHandle())) {
			info("capturing window is no longer alive"); // TODO: instead of dying - maybe retrying
			m_pGDICapture->SetCaptureHandle(NULL);
		}

		return 3;
	}

	return S_OK;
}

float CPushPinDesktop::GetFps() {
	return (float)(UNITS / m_rtFrameLength);
}

int CPushPinDesktop::getNegotiatedFinalWidth() {
	return width_;
}

int CPushPinDesktop::getNegotiatedFinalHeight() {
	return height_;
}

int CPushPinDesktop::getCaptureDesiredFinalWidth() {
	debug("getCaptureDesiredFinalWidth: %d", width_);
	return width_; // full/config setting, static
}

int CPushPinDesktop::getCaptureDesiredFinalHeight() {
	debug("getCaptureDesiredFinalHeight: %d", height_);
	return height_; // defaults to full/config static
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
