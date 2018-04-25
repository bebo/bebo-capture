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
// bebo-inject-capture
// 6f4437a3 - bd6f - 4fff - 8943 - 037f0f5cffe3

DEFINE_GUID(CLSID_PushSourceDesktop, 0x6f4437a3, 0xbd6f, 0x4fff, \
    0x89, 0x43, 0x03, 0x7f, 0x0f, 0x5c, 0xff, 0xe3);


// bebo-window-capture
// 6f4437a3 - bd6f - 4fff - 8943 - 037f0f5cffd2
DEFINE_GUID(CLSID_BeboWindowCapture, 0x6f4437a3, 0xbd6f, 0x4fff, \
	0x89, 0x43, 0x03, 0x7f, 0x0f, 0x5c, 0xff, 0xd2);

// bebo-screen-capture
// 6f4437a3 - bd6f - 4fff - 8943 - 037f0f5cffc1
DEFINE_GUID(CLSID_BeboScreenCapture, 0x6f4437a3, 0xbd6f, 0x4fff, \
	0x89, 0x43, 0x03, 0x7f, 0x0f, 0x5c, 0xff, 0xc1);

#else
// 8404812b-1627-41f5-8dba-0eb6d298fd1d
DEFINE_GUID(CLSID_PushSourceDesktop, 0x8404812b, 0x1627, 0x41f5, 0x8d, 0xba, 0x0e, 0xb6, 0xd2, 0x98, 0xfd, 0x1d);

// 8404812b-1627-41f5-8dba-0eb6d298fd0c
DEFINE_GUID(CLSID_BeboWindowCapture, 0x8404812b, 0x1627, 0x41f5, 0x8d, 0xba, 0x0e, 0xb6, 0xd2, 0x98, 0xfd, 0x0c);

// 8404812b-1627-41f5-8dba-0eb6d298fdfb
DEFINE_GUID(CLSID_BeboScreenCapture, 0x8404812b, 0x1627, 0x41f5, 0x8d, 0xba, 0x0e, 0xb6, 0xd2, 0x98, 0xfd, 0xfb);
#endif

#endif

#define DS_FILTER_DESCRIPTION    L"Bebo Inject Game Capture Direct Show Filter"
#define DS_FILTER_NAME           L"bebo-inject-capture"

#define DS_WINDOW_FILTER_DESCRIPTION    L"Bebo Window Game Capture Direct Show Filter"
#define DS_WINDOW_FILTER_NAME    L"bebo-window-capture"

#define DS_SCREEN_FILTER_DESCRIPTION    L"Bebo Screen Game Capture Direct Show Filter"
#define DS_SCREEN_FILTER_NAME    L"bebo-screen-capture"

#define DS_LOG_NAME              "bebo-capture"
