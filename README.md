


# Bebo Game Capture DLL

## Build Dependencies
* Visual Studio 2015 (C++)

## Run Bebo with your version of the capture DLL


run cmd as __Administrator__:
```
C:\Users\fpn\bebo-capture\x64\Release>regsvr32 BeboGameCapture.DLL
```
restart bebo

# Build new libyuv (notes)
* depot tools (for libyuv) https://www.chromium.org/developers/how-tos/install-depot-tools
* SET DEPOT_TOOLS_WIN_TOOLCHAIN=0
* gclient config --name src https://chromium.googlesource.com/libyuv/libyuv



















Registry Keys:

HKEY_CLASSES_ROOT\CLSID\{1F1383EF-8019-4F96-9F53-1F0DA2684163}
bebo-game-capture
HKEY_CLASSES_ROOT\CLSID\{1F1383EF-8019-4F96-9F53-1F0DA2684163}\InprocServer32
C:\Windows\SysWOW64\BeboCapture.DLL
ThreadingModel Both

HKEY_CLASSES_ROOT\WOW6432Node\CLSID\{8404812B-1627-41F5-8DBA-0EB6D298FD1D}
bebo-game-capture

HKEY_CLASSES_ROOT\WOW6432Node\CLSID\{8404812B-1627-41F5-8DBA-0EB6D298FD1D}\InprocServer32
C:\Windows\SysWow64\BeboCapture.DLL
ThreadingModel Both

HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\{1F1383EF-8019-4F96-9F53-1F0DA2684163}
bebo-game-capture

HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\{1F1383EF-8019-4F96-9F53-1F0DA2684163}\InprocServer32
C:\Windows\SysWOW64\BeboCapture.DLL
ThreadingModel Both



HKEY_LOCAL_MACHINE\SOFTWARE\Classes\WOW6432Node\CLSID\{860BB310-5D01-11d0-BD3B-00A0C911CE86}
VFW Capture Class Manager

HKEY_LOCAL_MACHINE\SOFTWARE\Classes\WOW6432Node\CLSID\{860BB310-5D01-11d0-BD3B-00A0C911CE86}\InprocServer32
C:\Windows\SysWOW64\devenum.dll
HKEY_LOCAL_MACHINE\SOFTWARE\Classes\WOW6432Node\CLSID\{860BB310-5D01-11d0-BD3B-00A0C911CE86}\Instance

HKEY_LOCAL_MACHINE\SOFTWARE\Classes\WOW6432Node\CLSID\{860BB310-5D01-11d0-BD3B-00A0C911CE86}\Instance\{8404812B-1627-41F5-8DBA-0EB6D298FD1D}

CLSID {8404812B-1627-41F5-8DBA-0EB6D298FD1D}
FilterData 
FriendlyName bebo-game-capture

