# CDAS WinUSB Installation Guide

This guide describes the active Windows 8/10/11 installation path for the CDAS
WinUSB package under `src_new`.

Legacy XP/Vista/Windows 7 driver packages are archived under `Old_files` and
are not covered by this guide.

## Supported Systems

- Windows 8
- Windows 10
- Windows 11

Both x86 and x64 INF sections are present in `src_new/sys/cdas_winusb.inf`, but
deployment should be validated on the exact target OS and architecture.

## Files Used

```text
src_new/sys/cdas_winusb.inf
src_new/include/winusb_compat.h
src_new/lib/winusb_compat.cpp
src_new/exe/capsousb_test.cpp
```

The INF binds the USB device to the Windows in-box `winusb.sys` driver. The
sample application talks to the device through the WinUSB API.

## Before Installation

1. Confirm the device hardware ID in Device Manager or with `usbview.exe`.
2. Compare it with the IDs in `src_new/sys/cdas_winusb.inf`.
3. Update the INF if the actual VID/PID is different.
4. Package and sign the driver package for the target Windows environment.

Current INF IDs:

- `USB\VID_03EB&PID_941C`
- `USB\VID_0638&PID_0931`
- `USB\VID_03EB&PID_952C`

## Signing Requirement

The repository includes an INF template but does not include a generated catalog
or private signing key.

For normal deployment, create a catalog and sign the package with the signing
method required by the target Windows version and security policy. Windows 10
and Windows 11 production deployment normally require an appropriately trusted
driver package.

For engineering-only testing, Windows test-signing mode may be used according
to the organization's driver test policy.

The current INF includes `PnpLockdown=1` and has been checked with WDK
`InfVerif.exe`.

## Install With pnputil

Open an elevated Command Prompt or PowerShell window.

From the repository root:

```bat
pnputil /add-driver src_new\sys\cdas_winusb.inf /install
```

Then unplug and reconnect the CDAS device if Windows does not immediately bind
the new driver.

## Verify Driver Binding

In Device Manager:

1. Find the CDAS USB device.
2. Open device properties.
3. Check the driver details.
4. Confirm that `winusb.sys` is listed.

You can also inspect the device with `usbview.exe` and confirm that the VID/PID
matches the INF.

## Build The Sample

The sample has been verified with VS2022 Build Tools.

```bat
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x86
MSBuild.exe src_new\exe\capsousb_test.vcxproj /p:Configuration=Debug /p:Platform=Win32 /t:Build
```

The build output is:

```text
src_new\exe\Debug\capsousb_test.exe
```

The repository also keeps a prebuilt copy at:

```text
dist\capsousb_test.exe
```

When the sample source changes, rebuild the executable and refresh the copy in
`dist`.

## Run The Sample

After the device is bound to `winusb.sys`, run the sample from an elevated or
appropriately permissioned command prompt if required by the local device access
policy.

```bat
dist\capsousb_test.exe
```

The sample keeps the original command flow in `capsousb_test.cpp`. The WinUSB
compatibility layer only replaces the USB transport layer.

## Troubleshooting

- If installation fails, confirm that the INF is signed and the catalog matches
  the INF.
- If Windows selects another driver, remove the old driver binding and install
  the WinUSB package again.
- If the sample cannot open the device, verify VID/PID, interface GUID, and
  device permissions.
- If transfers fail, confirm the device exposes the expected bulk-in and
  bulk-out endpoints.
- If multiple endpoints exist in the same direction, add explicit endpoint
  selection in `src_new/lib/winusb_compat.cpp`.
- If code calls legacy device-counting helper APIs, expect a "not implemented"
  result from this WinUSB sample layer.

## Rollback

Use Device Manager or `pnputil` to remove the installed WinUSB driver package if
rollback is required.

Example:

```bat
pnputil /enum-drivers
pnputil /delete-driver oemXX.inf /uninstall
```

Replace `oemXX.inf` with the published INF name shown by `pnputil`.
