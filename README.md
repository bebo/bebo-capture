# Bebo Game Capture Direct Show Filter

https://github.com/bebo/bebo-capture

This projects provides a Direct Show Capture Filter for capturing:
* Direct X and OpenGL games (Direct X, OpenGL)
* desktop capture (Using the Desktop Duplication API) - TODO
* windowed applications (GDI) - TODO (commented out at the moment)

Currently tested on Windows 10 64 bit

# Build

## Build Dependencies
* Visual Studio 2015 (C++)

## To register the capture DLL

run cmd as __Administrator__:
```
regsvr32 BeboGameCapture.DLL
```


## libyuv (notes)

You may want to build your own copy of libyuv 
* depot tools (for libyuv) https://www.chromium.org/developers/how-tos/install-depot-tools
* SET DEPOT_TOOLS_WIN_TOOLCHAIN=0
* gclient config --name src https://chromium.googlesource.com/libyuv/libyuv

# Attributions / History

* Really we want to use Chrome / NW.JS and webRTC to capture all games, but
  there are some caveats, so we built this for now until webrtc game capture
  support is seamless and complete
* We started this code based on the Direct Show Desktop Capture Filter:
  https://github.com/rdp/screen-capture-recorder-to-video-windows-free
* OBS https://obsproject.com is awesome at capturing the frames from a game,
  so we use that code for capturing frames from games
* All direct show filters make heavy use of the Micosoft DirectShow SDK
  BaseClasses
* We use the super fast g2log by Kjell Hedstroem for logging

# License

The source code provied by Pigs in Flight Inc. is licensed under the MIT
license.

Different parts of the project are under different licenses depending on their
respective origin.

For the full text of the licenses see: LICENSE.TXT
