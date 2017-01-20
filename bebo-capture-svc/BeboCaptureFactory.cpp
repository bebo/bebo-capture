#include "BeboCaptureFactory.h"
#include "BeboCapture.h"

HRESULT __stdcall CBeboCaptureFactory::CreateInstance(IUnknown* pUnknownOuter,
	const IID& iid,
	void** ppv)
{
	//
	//This method lets the client manufacture components en masse
	//The class factory provides a mechanism to control the way
	//the component is created. Within the class factory the 
	//author of the component may decide to selectivey enable
	//or disable creation as per license agreements 
	//
	//

	// Cannot aggregate.
	if (pUnknownOuter != NULL)
	{
		return CLASS_E_NOAGGREGATION;
	}

	//
	// Create an instance of the component.
	//
	CBeboCapture* pObject = new CBeboCapture;
	if (pObject == NULL)
	{
		return E_OUTOFMEMORY;
	}

	//
	// Get the requested interface.
	//
	return pObject->QueryInterface(iid, ppv);
}


HRESULT __stdcall CBeboCaptureFactory::LockServer(BOOL bLock)
{
	return E_NOTIMPL;
}
