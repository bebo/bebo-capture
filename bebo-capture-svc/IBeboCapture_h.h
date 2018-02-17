

/* this ALWAYS GENERATED file contains the definitions for the interfaces */


 /* File created by MIDL compiler version 8.01.0620 */
/* at Mon Jan 18 19:14:07 2038
 */
/* Compiler settings for IBeboCapture.idl:
    Oicf, W1, Zp8, env=Win64 (32b run), target_arch=AMD64 8.01.0620 
    protocol : dce , ms_ext, c_ext, robust
    error checks: allocation ref bounds_check enum stub_data 
    VC __declspec() decoration level: 
         __declspec(uuid()), __declspec(selectany), __declspec(novtable)
         DECLSPEC_UUID(), MIDL_INTERFACE()
*/
/* @@MIDL_FILE_HEADING(  ) */

#pragma warning( disable: 4049 )  /* more than 64k source lines */


/* verify that the <rpcndr.h> version is high enough to compile this file*/
#ifndef __REQUIRED_RPCNDR_H_VERSION__
#define __REQUIRED_RPCNDR_H_VERSION__ 475
#endif

#include "rpc.h"
#include "rpcndr.h"

#ifndef __RPCNDR_H_VERSION__
#error this stub requires an updated version of <rpcndr.h>
#endif /* __RPCNDR_H_VERSION__ */

#ifndef COM_NO_WINDOWS_H
#include "windows.h"
#include "ole2.h"
#endif /*COM_NO_WINDOWS_H*/

#ifndef __IBeboCapture_h_h__
#define __IBeboCapture_h_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1020)
#pragma once
#endif

/* Forward Declarations */ 

#ifndef __IBeboCapture_FWD_DEFINED__
#define __IBeboCapture_FWD_DEFINED__
typedef interface IBeboCapture IBeboCapture;

#endif 	/* __IBeboCapture_FWD_DEFINED__ */


#ifndef __IBeboCapture_FWD_DEFINED__
#define __IBeboCapture_FWD_DEFINED__
typedef interface IBeboCapture IBeboCapture;

#endif 	/* __IBeboCapture_FWD_DEFINED__ */


/* header files for imported files */
#include "oaidl.h"
#include "ocidl.h"

#ifdef __cplusplus
extern "C"{
#endif 


#ifndef __IBeboCapture_INTERFACE_DEFINED__
#define __IBeboCapture_INTERFACE_DEFINED__

/* interface IBeboCapture */
/* [object][helpstring][version][uuid][oleautomation] */ 


EXTERN_C const IID IID_IBeboCapture;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("f91d23ff-de1f-4ebb-be77-63401a62dd25")
    IBeboCapture : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE SetTarget( 
            /* [range][in] */ long size,
            /* [size_is][in] */ unsigned char *targetName) = 0;
        
    };
    
    
#else 	/* C style interface */

    typedef struct IBeboCaptureVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IBeboCapture * This,
            /* [in] */ REFIID riid,
            /* [annotation][iid_is][out] */ 
            _COM_Outptr_  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IBeboCapture * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IBeboCapture * This);
        
        HRESULT ( STDMETHODCALLTYPE *SetTarget )( 
            IBeboCapture * This,
            /* [range][in] */ long size,
            /* [size_is][in] */ unsigned char *targetName);
        
        END_INTERFACE
    } IBeboCaptureVtbl;

    interface IBeboCapture
    {
        CONST_VTBL struct IBeboCaptureVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IBeboCapture_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IBeboCapture_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IBeboCapture_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IBeboCapture_SetTarget(This,size,targetName)	\
    ( (This)->lpVtbl -> SetTarget(This,size,targetName) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IBeboCapture_INTERFACE_DEFINED__ */



#ifndef __BeboCaptureLib_LIBRARY_DEFINED__
#define __BeboCaptureLib_LIBRARY_DEFINED__

/* library BeboCaptureLib */
/* [helpstring][version][uuid] */ 



EXTERN_C const IID LIBID_BeboCaptureLib;
#endif /* __BeboCaptureLib_LIBRARY_DEFINED__ */

/* Additional Prototypes for ALL interfaces */

/* end of Additional Prototypes */

#ifdef __cplusplus
}
#endif

#endif


