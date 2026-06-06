# CDAS WinUSB Source Package

`src_new` is the active source package for the CDAS WinUSB sample.

It keeps the user-mode sample command flow and replaces the old custom WDM
kernel driver transport with Windows in-box `winusb.sys`.

## Supported Windows Versions

This source package is intended for:

- Windows 8
- Windows 10
- Windows 11

Windows XP, Windows Vista, and Windows 7 are not supported by this WinUSB
package. Historical files for those systems are archived under `..\Old_files`.

## Folder Layout

```text
src_new
|-- bulkusb.sln
|-- readme.md
|-- readme_cht.md
|-- exe
|   |-- capsousb_test.cpp
|   |-- capsousb_test.vcproj
|   |-- capsousb_test.vcxproj
|   |-- stdafx.cpp
|   |-- stdafx.h
|   `-- targetver.h
|-- include
|   |-- bulkusb_api.h
|   `-- winusb_compat.h
|-- lib
|   |-- bulkusb_lib.vcproj
|   |-- bulkusb_lib-2010.vcxproj
|   |-- stdafx.cpp
|   |-- stdafx.h
|   |-- targetver.h
|   `-- winusb_compat.cpp
`-- sys
    `-- cdas_winusb.inf
```

## Architecture

```text
exe/capsousb_test.cpp
  -> OpenBulkUSB / ReadFile / WriteFile / CloseHandle
  -> include/winusb_compat.h
  -> lib/winusb_compat.cpp
  -> WinUSB API
  -> winusb.sys
  -> CDAS USB device
```

The CDAS command protocol remains in `exe/capsousb_test.cpp`. The compatibility
layer changes only the transport path.

## Compatibility Layer

`include/winusb_compat.h` maps the old BulkUsb-style sample calls to
WinUSB-backed functions:

```cpp
#define OpenBulkUSB WinUsbCompatOpenBulkUSB
#define ReadFile WinUsbCompatReadFile
#define WriteFile WinUsbCompatWriteFile
#define CloseHandle WinUsbCompatCloseHandle
```

`lib/winusb_compat.cpp` handles:

1. Device-interface discovery with SetupAPI.
2. Device-path open with `CreateFile`.
3. WinUSB initialization with `WinUsb_Initialize`.
4. Bulk or interrupt endpoint selection by direction.
5. Data transfer with `WinUsb_ReadPipe` and `WinUsb_WritePipe`.

## INF

`sys/cdas_winusb.inf` binds supported CDAS USB devices to Windows in-box
`winusb.sys`.

Current hardware IDs:

- `USB\VID_03EB&PID_941C`
- `USB\VID_0638&PID_0931`
- `USB\VID_03EB&PID_952C`

Verify the actual VID/PID before deployment. The INF must be packaged and
signed for the target Windows environment.

The INF includes `PnpLockdown=1` and registers both docking-system and
production-test interface GUIDs.

## Build

The Visual Studio project files are kept for the legacy sample structure:

- `exe/capsousb_test.vcproj`
- `exe/capsousb_test.vcxproj`
- `lib/bulkusb_lib.vcproj`
- `lib/bulkusb_lib-2010.vcxproj`

The VS2022 `v143` toolset is used. MFC is not required.

Validated command:

```bat
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x86
MSBuild.exe exe\capsousb_test.vcxproj /p:Configuration=MFC_DLL_Debug /p:Platform=Win32 /t:Build
```

The sample links against:

- `setupapi.lib`
- `winusb.lib`

The INF has been verified with WDK `InfVerif.exe`.

The repository-level `dist/capsousb_test.exe` is a prebuilt copy of the Win32 debug
sample executable. Rebuild and refresh it when this source package changes.

## Installation

Use the repository-level installation guide:

- `..\installation-guide.md`
- `..\installation-guide_cht.md`

## Limitations

- `OpenBulkUSB` returns a compatibility-layer context pointer, not a real
  Windows kernel handle.
- The returned handle must only be used by the wrapped sample calls.
- Endpoint selection currently chooses the first matching bulk or interrupt
  endpoint by direction.
- If a device exposes multiple endpoints in the same direction, add explicit
  endpoint mapping in `lib/winusb_compat.cpp`.
- Device-counting helper APIs are compatibility stubs and return "not
  implemented".
- The original sample command flow is preserved and is not a full CDAS3 command
  implementation.
