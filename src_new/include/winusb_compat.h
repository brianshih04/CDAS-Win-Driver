#ifndef __WINUSB_COMPAT_H__
#define __WINUSB_COMPAT_H__

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

HANDLE WinUsbCompatOpenBulkUSB(int output);
void WinUsbCompatChooseUSBDevice(int type);
void WinUsbCompatSelectCDASVersion(int version);
BOOL WinUsbCompatReadFile(
    HANDLE handle,
    LPVOID buffer,
    DWORD bytesToRead,
    LPDWORD bytesRead,
    LPOVERLAPPED overlapped);
BOOL WinUsbCompatWriteFile(
    HANDLE handle,
    LPCVOID buffer,
    DWORD bytesToWrite,
    LPDWORD bytesWritten,
    LPOVERLAPPED overlapped);
BOOL WinUsbCompatCloseHandle(HANDLE handle);

#ifdef __cplusplus
}
#endif

/*
   Minimal sample-code compatibility mode.

   Include this header after Windows headers and after bulkusb_api.h.
   It keeps the old sample call sites readable while routing USB I/O
   through WinUSB instead of the legacy bulkusb kernel driver.
 */
#define OpenBulkUSB WinUsbCompatOpenBulkUSB
#define ChooseUSBDevice WinUsbCompatChooseUSBDevice
#define SelectCDASVersion WinUsbCompatSelectCDASVersion
#define ReadFile WinUsbCompatReadFile
#define WriteFile WinUsbCompatWriteFile
#define CloseHandle WinUsbCompatCloseHandle

#endif
