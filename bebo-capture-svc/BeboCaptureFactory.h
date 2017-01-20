#pragma once
#include "Unknwn.h"
class CBeboCaptureFactory :
	public IClassFactory
{
public:

	//interface IUnknown methods 
	HRESULT __stdcall QueryInterface(
		REFIID riid,
		void **ppObj);
	ULONG   __stdcall AddRef();
	ULONG   __stdcall Release();


	//interface IClassFactory methods 
	HRESULT __stdcall CreateInstance(IUnknown* pUnknownOuter,
		const IID& iid,
		void** ppv);
	HRESULT __stdcall LockServer(BOOL bLock);

private:
	long m_nRefCount;

};

