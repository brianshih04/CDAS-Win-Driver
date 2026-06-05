# CDAS WinUSB Sample

This folder is a trimmed WinUSB-based sample package for the CDAS USB device.
It keeps the original sample command flow, but replaces the legacy custom
kernel driver dependency with a small WinUSB compatibility layer.

## What This Folder Contains

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

## What Was Removed

The original WDM kernel driver source is intentionally not included in this
folder. Files such as `bulkusb.c`, `bulkrwr.c`, `bulkpnp.c`, `bulkpwr.c`,
`bulkdev.c`, WDM `sources`, old MOF/resource files, and the old BulkUsb HTML
documentation were removed from this WinUSB sample package.

The original source tree remains in `..\src`.

## Architecture

```text
capsousb_test.cpp
  -> OpenBulkUSB / ReadFile / WriteFile / CloseHandle
  -> winusb_compat.h macro layer
  -> winusb_compat.cpp
  -> WinUSB API
  -> Windows in-box winusb.sys
  -> CDAS USB device
```

The sample command protocol remains in `exe\capsousb_test.cpp`. The compatibility
layer only replaces the USB transport layer.

## Compatibility Layer

`include\winusb_compat.h` maps the old sample calls to WinUSB-backed functions:

```cpp
#define OpenBulkUSB WinUsbCompatOpenBulkUSB
#define ReadFile WinUsbCompatReadFile
#define WriteFile WinUsbCompatWriteFile
#define CloseHandle WinUsbCompatCloseHandle
```

`lib\winusb_compat.cpp` implements those functions by:

1. Finding the WinUSB device interface with SetupAPI.
2. Opening the device path with `CreateFile`.
3. Calling `WinUsb_Initialize`.
4. Selecting the first bulk or interrupt endpoint matching the requested direction.
5. Calling `WinUsb_ReadPipe` or `WinUsb_WritePipe`.

## INF

`sys\cdas_winusb.inf` is an example INF that binds CDAS devices to the Windows
in-box WinUSB driver.

The included hardware IDs are:

- `USB\VID_03EB&PID_941C`
- `USB\VID_0638&PID_0931`

Verify the hardware IDs before deployment. The INF must be packaged and signed
appropriately for the target Windows version.

## Supported Windows Versions

This WinUSB-based replacement is intended for Windows 8, Windows 10, and
Windows 11 only. It relies on the Windows in-box `winusb.sys` driver and the
modern WinUSB INF binding model.

For Windows XP, Windows Vista, or Windows 7 deployments, keep using the original
legacy driver package under `..\src` / `..\installation*` unless a separate
signed WinUSB package is prepared and validated for that OS.

## Build Notes

The Visual Studio projects were kept so the sample can be built with the legacy
project structure:

- `exe\capsousb_test.vcproj`
- `exe\capsousb_test.vcxproj`
- `lib\bulkusb_lib.vcproj`
- `lib\bulkusb_lib-2010.vcxproj`

The projects have been updated to use the VS2022 `v143` toolset and no longer
require MFC. The sample application links against:

- `setupapi.lib`
- `winusb.lib`

Local build verification was completed with:

```bat
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x86
MSBuild.exe exe\capsousb_test.vcxproj /p:Configuration=MFC_DLL_Debug /p:Platform=Win32 /t:Build
```

The build succeeded and produced `exe\MFC_DLL_Debug\test_exe.exe`. Build output
files are intentionally not kept in this source package.

## Important Limitations

- The returned `HANDLE` from `OpenBulkUSB` is a compatibility-layer context
  pointer, not a real Windows kernel handle.
- Only the wrapped calls in this sample should use that handle.
- Do not pass the returned handle to arbitrary Win32 APIs.
- The compatibility layer currently selects the first matching bulk or interrupt
  endpoint by direction.
- If the CDAS device exposes multiple matching endpoints, add explicit endpoint
  mapping.
- The original command flow is preserved, including existing sample limitations
  such as hard-coded serial-number behavior.

## Relationship To The Original Driver

The original custom WDM driver in `..\src\sys` handled USB bulk transport in
kernel mode. This sample uses `winusb.sys` for that transport instead.

The CDAS command flow is still user-mode code in `exe\capsousb_test.cpp`; it was
not moved into a kernel driver.
