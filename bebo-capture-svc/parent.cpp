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

CGameCapture::CGameCapture(IUnknown *pUnk, HRESULT *phr)
           : CSource(NAME("PushSourceDesktop Parent"), pUnk, CLSID_PushSourceDesktop)
{
    // The pin magically adds itself to our pin array.
	// except its not an array since we just have one [?]
    m_pPin = new CPushPinDesktop(phr, this);

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


CUnknown * WINAPI CGameCapture::CreateInstance(IUnknown *pUnk, HRESULT *phr)
{
	// the first entry point
	setupLogging();
    CGameCapture *pNewFilter = new CGameCapture(pUnk, phr);

	if (phr)
	{
		if (pNewFilter == NULL) 
			*phr = E_OUTOFMEMORY;
		else
			*phr = S_OK;
	}
    return pNewFilter;
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
