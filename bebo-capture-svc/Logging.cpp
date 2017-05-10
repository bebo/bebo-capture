#include "Logging.h"
#include "windows.h"

#ifndef RELEASE
#define RELEASE "v0.1.2"
#endif

extern HRESULT RegGetBeboSZ(LPCTSTR szValueName, LPBYTE data, LPDWORD datasize);

std::unique_ptr<g2LogWorker> logworker = NULL;


void logRotate() {
	if (logworker == NULL) {
		return;
	}
	const DWORD SIZE = 2048;
	DWORD size = SIZE;
	BYTE data[SIZE];
	CHAR c_filename[SIZE];
	memset(data, 0, size);
	memset(c_filename, 0, size);
	if (RegGetBeboSZ(TEXT("Logs"), data, &size) == S_OK) {
		wsprintfA(c_filename, "%S\\", data);
		logworker->changeLogFile(c_filename);
	}
}

void setupLogging() {

	if (logworker == NULL || &*logworker == NULL) {
		const DWORD SIZE = 2048;
		DWORD size = SIZE;
		BYTE data[SIZE];
		CHAR c_filename[SIZE];
		memset(data, 0, size);
		memset(c_filename, 0, size);
		if (RegGetBeboSZ(TEXT("Logs"), data, &size) == S_OK) {
			wsprintfA(c_filename, "%S\\", data);
		}
		std::unique_ptr<g2LogWorker> g2log(new g2LogWorker("sarlacc", c_filename));
		logworker = std::move(g2log);
		g2::initializeLogging(&*logworker);
		info("Version: %s", RELEASE);
		char filename[4096];
		GetModuleFileNameA(NULL, filename, 4096);
		info("Executable: %s", filename);
	}
}