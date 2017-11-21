#include <objbase.h>
#include "BeboCapture.h"
#include "IBeboCapture_i.c"
#include "combase.h"

#include "Logging.h"

HRESULT CBeboCapture::SetTarget(long size, unsigned char * targetName) {
	info("setTargetName %S", targetName);
	return S_OK;
}

CBeboCapture::CBeboCapture(IUnknown *pUnk, HRESULT *phr):
    CUnknown(NAME("Bebo Capture"), pUnk, phr)
{
	m_nRefCount = 0;
//	InterlockedIncrement(&g_nComObjsInUse);
}

CBeboCapture::~CBeboCapture()
{
//	InterlockedDecrement(&g_nComObjsInUse);
}

STDMETHODIMP CBeboCapture::NonDelegatingQueryInterface(
	REFIID riid,
	__deref_out void ** ppv)
{
	if (riid == IID_IBeboCapture) {
		return GetInterface((IBeboCapture *) this, ppv);
	} else {
		return CUnknown::NonDelegatingQueryInterface(riid, ppv);
	}
}



/* This goes in the factory template table to create new instances */

CUnknown * WINAPI CBeboCapture::CreateInstance(LPUNKNOWN pUnk, HRESULT *phr)
{
	CBeboCapture *pNewAPI = new CBeboCapture(pUnk, phr);

	if (phr)
	{
		if (pNewAPI == NULL) 
			*phr = E_OUTOFMEMORY;
		else
			*phr = S_OK;
	}
    return pNewAPI;
}

#if 0
HRESULT __stdcall CBeboCapture::QueryInterface(
	REFIID riid,
	void **ppObj)
{
	if (riid == IID_IUnknown)
	{
		//*ppObj = static_cast(this);
		*ppObj = (CBeboCapture*)this;
		AddRef();
		return S_OK;
	}
	if (riid == IID_IBeboCapture)
	{
		*ppObj = (CBeboCapture*)this;
		AddRef();
		return S_OK;
	}
	//
	//if control reaches here then , let the client know that
	//we do not satisfy the required interface
	//
	*ppObj = NULL;
	return E_NOINTERFACE;
}//QueryInterface method
ULONG   __stdcall CBeboCapture::AddRef()
{
	return InterlockedIncrement(&m_nRefCount);
}


ULONG   __stdcall CBeboCapture::Release()
{
	long nRefCount = 0;
	nRefCount = InterlockedDecrement(&m_nRefCount);
	if (nRefCount == 0) delete this;
	return nRefCount;
}
#endif