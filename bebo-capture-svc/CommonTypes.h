// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved

#ifndef _COMMONTYPES_H_
#define _COMMONTYPES_H_

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <sal.h>
#include <new>
#include <warning.h>
#include <DirectXMath.h>

//
// Holds info about the pointer/cursor
//
typedef struct _PtrInfo
{
	_Field_size_bytes_(BufferSize) BYTE* PtrShapeBuffer;
	DXGI_OUTDUPL_POINTER_SHAPE_INFO ShapeInfo;
	POINT Position;
	bool Visible;
	UINT BufferSize;
	UINT WhoUpdatedPositionLast;
	LARGE_INTEGER LastTimeStamp;
} PtrInfo;

//
// FRAME_DATA holds information about an acquired frame
//
typedef struct _FrameData
{
	ID3D11Texture2D* Frame;
	DXGI_OUTDUPL_FRAME_INFO FrameInfo;
	_Field_size_bytes_((MoveCount * sizeof(DXGI_OUTDUPL_MOVE_RECT)) + (DirtyCount * sizeof(RECT))) BYTE* MetaData;
	UINT DirtyCount;
	UINT MoveCount;
} FrameData;

#endif

