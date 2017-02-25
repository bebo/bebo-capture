#include "Logging.h"
#include "windows.h"

#ifndef RELEASE
#define RELEASE "v0.1.1"
#endif

extern HRESULT RegGetBeboSZ(LPCTSTR szValueName, LPBYTE data, LPDWORD datasize);

std::unique_ptr<g2LogWorker> logworker = NULL;

void setupLogging() {

	if (logworker == NULL || &*logworker == NULL) {
		DWORD size = 2048;
		BYTE data[2048];
		CHAR c_filename[2048];
		if (RegGetBeboSZ(TEXT("Logs"), data, &size) == S_OK) {
			wsprintfA(c_filename, "%S\\", data);
		}
		std::unique_ptr<g2LogWorker> g2log(new g2LogWorker("sarlacc", c_filename));
		logworker = std::move(g2log);
		g2::initializeLogging(&*logworker);
		info("Version: %s", RELEASE);
	}
}