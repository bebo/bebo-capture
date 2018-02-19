#include <streams.h>
#include "Capture.h"
#include "names_and_ids.h"
#include "DibHelper.h"
#include "Logging.h"

/**********************************************
 *
 *  CGameCapture Class Parent
 *
 **********************************************/

CGameCapture::CGameCapture(IUnknown *pUnk, HRESULT *phr, const CLSID* filter_clsid, int capture_type)
           : CSource(NAME("PushSourceDesktop Parent"), pUnk, CLSID_PushSourceDesktop)
{
    // The pin magically adds itself to our pin array.
	// except its not an array since we just have one [?]
    m_pPin = new CPushPinDesktop(phr, this, capture_type);

	if (phr)
	{
		if (m_pPin == NULL)
			*phr = E_OUTOFMEMORY;
		else
			*phr = S_OK;
	}  
}


CGameCapture::~CGameCapture() // parent destructor
{
	// COM should call this when the refcount hits 0...
	// but somebody should make the refcount 0...
    delete m_pPin;
}


CUnknown * WINAPI CGameCapture::CreateInstance(IUnknown *pUnk, HRESULT *phr, const CLSID* filter_clsid, int capture_type)
{
	// the first entry point
	setupLogging();
    CGameCapture *pNewFilter = new CGameCapture(pUnk, phr, filter_clsid, capture_type);

	if (phr)
	{
		if (pNewFilter == NULL) 
			*phr = E_OUTOFMEMORY;
		else
			*phr = S_OK;
	}
    return pNewFilter;
}


CUnknown * WINAPI CGameCapture::CreateInstanceInject(IUnknown *pUnk, HRESULT *phr)
{
	return CreateInstance(pUnk, phr, &CLSID_PushSourceDesktop, CAPTURE_INJECT);
}

CUnknown * WINAPI CGameCapture::CreateInstanceWindow(IUnknown *pUnk, HRESULT *phr)
{
	return CreateInstance(pUnk, phr, &CLSID_BeboWindowCapture, CAPTURE_GDI);
}

CUnknown * WINAPI CGameCapture::CreateInstanceScreen(IUnknown *pUnk, HRESULT *phr)
{
	return CreateInstance(pUnk, phr, &CLSID_BeboScreenCapture, CAPTURE_DESKTOP);
}


HRESULT CGameCapture::QueryInterface(REFIID riid, void **ppv)
{
    //Forward request for IAMStreamConfig & IKsPropertySet to the pin
    if(riid == _uuidof(IAMStreamConfig) || riid == _uuidof(IKsPropertySet)) {
        return m_paStreams[0]->QueryInterface(riid, ppv);
	}
    else {
        return CSource::QueryInterface(riid, ppv);
	}

}
