# Bebo Game Capture Direct Show Filter Service

https://github.com/bebo/bebo-capture

This projects provides a Direct Show Capture Filter Service for capturing:
* Direct X and OpenGL games (Direct X, OpenGL)
* desktop capture (Using the Desktop Duplication API) - TODO
* windowed applications (GDI) - TODO (commented out at the moment)

Currently tested on Windows 10 64 bit

# Build

## Build Dependencies
* Visual Studio 2015 (C++)

## To register the capture DLL as a Direct Show Capture Service

run cmd as __Administrator__:
```
regsvr32 BeboGameCapture.DLL
```

## Registry Values


* CaptureType
 * "desktop" -> DXGI Desktop Duplication API
 * "inject" -> graphics hook injection
 * "gdi" -> gdi
 * "dshow" -> libdshow

 - defaults to "inject" for backwards compatibility

## libyuv (notes)


You may want to build your own copy of libyuv 
* last libyuv verion used: 54289f1bb0c78afdab73839c67989527f3237912
* depot tools (for libyuv) https://www.chromium.org/developers/how-tos/install-depot-tools
* also see https://chromium.googlesource.com/libyuv/libyuv/+/master/docs/getting_started.md

```
SET DEPOT_TOOLS_WIN_TOOLCHAIN=0
set GYP_DEFINES=clang=1 target_arch=x64
gclient config --name src https://chromium.googlesource.com/libyuv/libyuv
gclient sync
cd src
call python tools\clang\scripts\update.py
call gn gen out/Release "--args=is_debug=false is_official_build=true is_clang=true target_cpu=\"x64\""
ninja -v -C out/Release
```
find libyuv_internal.lib and the include directory


# Attributions / History

* Really we want to use Chrome / chromium / NW.JS and webRTC to capture all
  games, but there are some caveats, so we built this for now until webrtc game
  capture support is seamless and complete
* We started this code based on the Direct Show Desktop Capture Filter:
  https://github.com/rdp/screen-capture-recorder-to-video-windows-free
* OBS https://obsproject.com is awesome at capturing the frames from a game,
  so we use that code for capturing frames from games
* All direct show filters make heavy use of the Micosoft DirectShow SDK
  BaseClasses
* We use the super fast g2log by Kjell Hedstroem for logging
* We use several of the chromium base classes / infrastructure

# License

The source code provied by Pigs in Flight Inc. is licensed under the MIT
license.

Different parts of the project are under different licenses depending on their
respective origin.

For the full text of the licenses see: LICENSE.TXT
