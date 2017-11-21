//------------------------------------------------------------------------------
// File: DibHelper.H
//
// Desc: DirectShow sample code - Helper code for bitmap manipulation
//
// Copyright (c) Microsoft Corporation.  All rights reserved.
//------------------------------------------------------------------------------

#define HDIB HANDLE	

/* DIB macros */
#define IS_WIN30_DIB(lpbi)  ((*(LPDWORD)(lpbi)) == sizeof(BITMAPINFOHEADER))
#define RECTWIDTH(lpRect)   ((lpRect)->right - (lpRect)->left)
#define RECTHEIGHT(lpRect)  ((lpRect)->bottom - (lpRect)->top)

typedef unsigned __int64 QWORD;

void WarmupCounter();
__int64 StartCounter();
long double GetCounterSinceStartMillis(__int64 start);
void AddMouse(HDC hMemDC, LPRECT lpRect, HDC hScrDC, HWND hwnd);

#define ASSERT_RAISE(cond) \
    do \
    { \
        if (!(cond)) \
        { \
            const size_t len = 1256;\
            wchar_t buffer[len] = {};\
	        _snwprintf_s(buffer, len - 1, L"assert failed, please fix (or report): %ls %ls %d", TEXT(#cond), TEXT(__FILE__), __LINE__);\
			throw std::invalid_argument( "received negative value" );\
        } \
    } while(0);

#define ASSERT_RETURN(cond) \
    do \
    { \
        if (!(cond)) \
        { \
            const size_t len = 1256;\
            wchar_t buffer[len] = {};\
			return E_INVALIDARG;\
        } \
    } while(0);