# CDAS WinUSB Driver Package

This repository contains the current WinUSB-based sample package for the
CapsoVision Capsule Data Access System (CDAS) USB docking device.

The active implementation is under `src_new`. Legacy WDM driver sources,
old installers, and historical binary packages have been archived under
`Old_files` and are not part of the current active package.

## Active Scope

The current package replaces the old custom kernel driver path with the
Windows in-box `winusb.sys` driver plus a small user-mode compatibility layer.
The sample command flow remains in user mode.

```text
CDAS sample application
  -> BulkUsb-compatible API names
  -> src_new/include/winusb_compat.h
  -> src_new/lib/winusb_compat.cpp
  -> WinUSB API
  -> Windows in-box winusb.sys
  -> CDAS USB device
```

## Supported Windows Versions

This WinUSB package is intended for:

- Windows 8
- Windows 10
- Windows 11

Windows XP, Windows Vista, and Windows 7 are not supported by this active
WinUSB package. Historical files for those systems are kept only in
`Old_files`.

## Repository Layout

```text
.
|-- README.md
|-- README_cht.md
|-- installation-guide.md
|-- installation-guide_cht.md
|-- CDAS-Win-Driver-analysis.md
|-- dist
|   `-- capsousb_test.exe
|-- Doc
|   |-- CDAS3 New command_V16_AVISION.xlsx
|   |-- USB docking system driver Guide.docx
|   `-- USB docking system driver Guide.pdf
|-- src_new
|   |-- readme.md
|   |-- readme_cht.md
|   |-- sys
|   |   `-- cdas_winusb.inf
|   |-- include
|   |-- lib
|   `-- exe
`-- Old_files
```

Do not treat `Old_files` as active source. It is retained for reference,
comparison, and recovery only.

## Main Files

- `src_new/sys/cdas_winusb.inf`: WinUSB INF template for binding CDAS devices
  to `winusb.sys`.
- `src_new/include/winusb_compat.h`: compatibility macro layer that maps the
  old sample calls to WinUSB-backed functions.
- `src_new/lib/winusb_compat.cpp`: SetupAPI and WinUSB transport
  implementation.
- `src_new/exe/capsousb_test.cpp`: sample command-flow code.
- `dist/capsousb_test.exe`: prebuilt Win32 debug sample executable.
- `installation-guide.md`: installation and verification procedure.
- `CDAS-Win-Driver-analysis.md`: project analysis report.

## Hardware IDs

The WinUSB INF currently includes:

- `USB\VID_03EB&PID_941C`
- `USB\VID_0638&PID_0931`
- `USB\VID_03EB&PID_952C`

Verify the actual device VID/PID before packaging or deployment.

`src_new/sys/cdas_winusb.inf` registers the docking-system interface GUID and
the production-test interface GUID used by the WinUSB compatibility layer.

## Build Summary

The Visual Studio projects in `src_new` use the VS2022 `v143` toolset and
plain Win32/WinUSB APIs.

Validated local build target:

```bat
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x86
MSBuild.exe src_new\exe\capsousb_test.vcxproj /p:Configuration=Debug /p:Platform=Win32 /t:Build
```

The sample links against:

- `setupapi.lib`
- `winusb.lib`

The package was verified with WDK `InfVerif.exe`; the INF is valid.

A prebuilt sample executable is kept at `dist/capsousb_test.exe`. Rebuild it from
`src_new/exe/capsousb_test.vcxproj` whenever the sample source changes.

## Installation

See `installation-guide.md` for the Windows 8/10/11 WinUSB installation flow.

Short version:

1. Confirm the CDAS VID/PID.
2. Package and sign `src_new/sys/cdas_winusb.inf`.
3. Install with `pnputil`.
4. Verify that the device is using `winusb.sys`.
5. Run `dist/capsousb_test.exe` or a locally rebuilt sample application.

## Important Notes

- `OpenBulkUSB` returns a compatibility-layer context pointer, not a real
  Windows kernel handle.
- Only the wrapped `ReadFile`, `WriteFile`, and `CloseHandle` calls in this
  sample should use that handle.
- The compatibility layer selects the first bulk or interrupt endpoint that
  matches the requested direction.
- Legacy helper APIs for device counting are present only as compatibility
  stubs and return "not implemented".
- The old custom WDM driver is not used by the active WinUSB package.
