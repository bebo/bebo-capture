//------------------------------------------------------------------------------
// File: CaptureGuids.h
//
// Desc: DirectShow sample code - GUID definitions for PushSource filter set
//
// Copyright (c) Microsoft Corporation.  All rights reserved.
//------------------------------------------------------------------------------

#pragma once

#ifndef __CAPTURE_GUIDS_DEFINED
#define __CAPTURE_GUIDS_DEFINED

#ifdef _WIN64
// 1f1383ef-8019-4f96-9f53-1f0da2684163
DEFINE_GUID(CLSID_PushSourceDesktop, 0x1f1383ef, 0x8019, 0x4f96, 0x9f, 0x53, 0x1f, 0x0d, 0xa2, 0x68, 0x41, 0x63);

#else
// 8404812b-1627-41f5-8dba-0eb6d298fd1d
DEFINE_GUID(CLSID_PushSourceDesktop, 0x8404812b, 0x1627, 0x41f5, 0x8d, 0xba, 0x0e, 0xb6, 0xd2, 0x98, 0xfd, 0x1d);
#endif

#endif
