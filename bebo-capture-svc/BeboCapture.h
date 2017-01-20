#pragma once
#include "streams.h"
#include "IBeboCapture.h"

#define g_wszBeboCaptureApi L"Bebo Game Capture COM API"

// http://www.codeguru.com/cpp/com-tech/activex/tutorials/article.php/c5567/Step-by-Step-COM-Tutorial.htm

//extern long g_nComObjsInUse;

class CBeboCapture : public CUnknown, public IBeboCapture
{
private:
    CBeboCapture(IUnknown *pUnk, HRESULT *phr);
	~CBeboCapture();
public:

	//IUnknown interface 

	/* This goes in the factory template table to create new instances */
	static CUnknown * WINAPI CreateInstance(LPUNKNOWN lpunk,  HRESULT *phr);

	DECLARE_IUNKNOWN
	STDMETHOD(NonDelegatingQueryInterface)(REFIID riid, void ** ppv);

#if 0
	HRESULT __stdcall QueryInterface(
						REFIID riid,
						void **ppObj);
	ULONG   __stdcall AddRef();
	ULONG   __stdcall Release();
#endif

	HRESULT  __stdcall SetTarget(long size, unsigned char *targetName);
private:
	long m_nRefCount;   //for managing the reference count
};

