#include "Logging.h"
#include "windows.h"

extern HMODULE g_hModule;
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
	} else {
		GetTempPathA(SIZE, c_filename);
	}
	logworker->changeLogFile(c_filename);
}

void PrintFileVersion(TCHAR *pszFilePath)
{
	DWORD               dwSize = 0;
	BYTE                *pbVersionInfo = NULL;
	VS_FIXEDFILEINFO    *pFileInfo = NULL;
	UINT                puLenFileInfo = 0;

	// Get the version information for the file requested
	dwSize = GetFileVersionInfoSize(pszFilePath, NULL);
	if (dwSize == 0)
	{
		error("Error in GetFileVersionInfoSize: %d", GetLastError());
		return;
	}

	pbVersionInfo = new BYTE[dwSize];

	if (!GetFileVersionInfo(pszFilePath, 0, dwSize, pbVersionInfo))
	{
		error("Error in GetFileVersionInfo: %d", GetLastError());
		delete[] pbVersionInfo;
		return;
	}

	if (!VerQueryValue(pbVersionInfo, TEXT("\\"), (LPVOID*)&pFileInfo, &puLenFileInfo))
	{
		error("Error in VerQueryValue: %d", GetLastError());
		delete[] pbVersionInfo;
		return;
	}

	// pFileInfo->dwFileVersionMS is usually zero. However, you should check
	// this if your version numbers seem to be wrong

	info("Version: %d.%d.%d.%d",
		(pFileInfo->dwFileVersionMS >> 16) & 0xff,
		(pFileInfo->dwFileVersionMS >> 0) & 0xff,
		(pFileInfo->dwFileVersionLS >> 16) & 0xff,
		(pFileInfo->dwFileVersionLS >> 0) & 0xff
	);
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
		} else {
			GetTempPathA(SIZE, c_filename);
		}
		std::unique_ptr<g2LogWorker> g2log(new g2LogWorker("sarlacc", c_filename));
		logworker = std::move(g2log);
		g2::initializeLogging(&*logworker);
		wchar_t dllfilename[4096];
		GetModuleFileName(g_hModule, dllfilename, 4096);
		PrintFileVersion(dllfilename);

		wchar_t filename[4096];
		GetModuleFileName(NULL, filename, 4096);
		info("Executable: %S", filename);
	}
}