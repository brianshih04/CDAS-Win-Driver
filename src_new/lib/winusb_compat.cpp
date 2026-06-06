#include "stdafx.h"

#include <windows.h>
#include <setupapi.h>
#include <winusb.h>
#include <usb.h>
#include <tchar.h>
#include <stdlib.h>

#include "..\include\bulkusb_api.h"
#include "..\include\winusb_compat.h"

#undef OpenBulkUSB
#undef ReadFile
#undef WriteFile
#undef CloseHandle

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "winusb.lib")

static const GUID GUID_CLASS_DOCKING_SYSTEM =
    {0x873fdf, 0x62a8, 0x11d1, {0xaa, 0x5e, 0x00, 0xc0, 0x4f, 0xb1, 0x72, 0x8b}};

static const GUID GUID_CLASS_PRODUCTION_TEST =
    {0xc20e4f22, 0x44b8, 0x4feb, {0x8f, 0x31, 0xf7, 0x8e, 0x29, 0xcc, 0xd0, 0x35}};

typedef struct _WINUSB_COMPAT_CONTEXT {
    DWORD Signature;
    HANDLE DeviceHandle;
    WINUSB_INTERFACE_HANDLE InterfaceHandle;
    UCHAR PipeId;
    BOOL IsInput;
} WINUSB_COMPAT_CONTEXT, *PWINUSB_COMPAT_CONTEXT;

#define WINUSB_COMPAT_SIGNATURE 'SWCV'

static int g_deviceType = 0;
static int g_cdasVersion = 0;

static BOOL IsInputPipe(UCHAR pipeId)
{
    return (pipeId & USB_ENDPOINT_DIRECTION_MASK) != 0;
}

static BOOL HardwareIdMatches(HDEVINFO infoSet, PSP_DEVINFO_DATA infoData)
{
    TCHAR hardwareId[1024];
    DWORD propertyType = 0;
    DWORD requiredSize = 0;

    if (!SetupDiGetDeviceRegistryProperty(
            infoSet,
            infoData,
            SPDRP_HARDWAREID,
            &propertyType,
            (PBYTE)hardwareId,
            sizeof(hardwareId),
            &requiredSize)) {
        return g_cdasVersion == 0;
    }

    if (g_cdasVersion == 1) {
        return _tcsstr(hardwareId, CDAS1_VID_PID) != NULL;
    }

    if (g_cdasVersion == 2) {
        return _tcsstr(hardwareId, CDAS2_VID_PID) != NULL;
    }

    if (g_deviceType == 1) {
        return _tcsstr(hardwareId, CDAS_PRODUCTION_TEST_VID_PID) != NULL;
    }

    return TRUE;
}

static BOOL OpenInterfacePath(
    HDEVINFO infoSet,
    PSP_DEVICE_INTERFACE_DATA interfaceData,
    PSP_DEVINFO_DATA infoData,
    HANDLE* deviceHandle)
{
    PSP_DEVICE_INTERFACE_DETAIL_DATA detailData = NULL;
    DWORD requiredLength = 0;
    BOOL success = FALSE;

    *deviceHandle = INVALID_HANDLE_VALUE;

    SetupDiGetDeviceInterfaceDetail(
        infoSet,
        interfaceData,
        NULL,
        0,
        &requiredLength,
        NULL);

    if (requiredLength == 0) {
        return FALSE;
    }

    detailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(requiredLength);
    if (!detailData) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

    if (SetupDiGetDeviceInterfaceDetail(
            infoSet,
            interfaceData,
            detailData,
            requiredLength,
            NULL,
            infoData)) {
        *deviceHandle = CreateFile(
            detailData->DevicePath,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
            NULL);
        success = (*deviceHandle != INVALID_HANDLE_VALUE);
    }

    free(detailData);
    return success;
}

static BOOL SelectPipe(
    WINUSB_INTERFACE_HANDLE interfaceHandle,
    BOOL input,
    UCHAR* pipeId)
{
    USB_INTERFACE_DESCRIPTOR interfaceDescriptor;

    if (!WinUsb_QueryInterfaceSettings(interfaceHandle, 0, &interfaceDescriptor)) {
        return FALSE;
    }

    for (UCHAR index = 0; index < interfaceDescriptor.bNumEndpoints; ++index) {
        WINUSB_PIPE_INFORMATION pipeInfo;

        if (!WinUsb_QueryPipe(interfaceHandle, 0, index, &pipeInfo)) {
            continue;
        }

        if (pipeInfo.PipeType != UsbdPipeTypeBulk &&
            pipeInfo.PipeType != UsbdPipeTypeInterrupt) {
            continue;
        }

        if (IsInputPipe(pipeInfo.PipeId) == input) {
            *pipeId = pipeInfo.PipeId;
            return TRUE;
        }
    }

    SetLastError(ERROR_NOT_FOUND);
    return FALSE;
}

static HANDLE OpenWinUsbPipe(const GUID* interfaceGuid, BOOL input)
{
    HDEVINFO infoSet;
    SP_DEVICE_INTERFACE_DATA interfaceData;
    DWORD index = 0;
    DWORD lastError = ERROR_NOT_FOUND;

    infoSet = SetupDiGetClassDevs(
        interfaceGuid,
        NULL,
        NULL,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

    if (infoSet == INVALID_HANDLE_VALUE) {
        return INVALID_HANDLE_VALUE;
    }

    interfaceData.cbSize = sizeof(interfaceData);

    while (SetupDiEnumDeviceInterfaces(infoSet, NULL, interfaceGuid, index, &interfaceData)) {
        SP_DEVINFO_DATA infoData;
        HANDLE deviceHandle = INVALID_HANDLE_VALUE;
        WINUSB_INTERFACE_HANDLE winusbHandle = NULL;
        UCHAR pipeId = 0;

        infoData.cbSize = sizeof(infoData);

        if (!OpenInterfacePath(infoSet, &interfaceData, &infoData, &deviceHandle)) {
            lastError = GetLastError();
            ++index;
            continue;
        }

        if (!HardwareIdMatches(infoSet, &infoData)) {
            lastError = ERROR_NOT_FOUND;
            ::CloseHandle(deviceHandle);
            ++index;
            continue;
        }

        if (!WinUsb_Initialize(deviceHandle, &winusbHandle)) {
            lastError = GetLastError();
            ::CloseHandle(deviceHandle);
            ++index;
            continue;
        }

        if (SelectPipe(winusbHandle, input, &pipeId)) {
            PWINUSB_COMPAT_CONTEXT context =
                (PWINUSB_COMPAT_CONTEXT)malloc(sizeof(WINUSB_COMPAT_CONTEXT));

            if (!context) {
                WinUsb_Free(winusbHandle);
                ::CloseHandle(deviceHandle);
                SetupDiDestroyDeviceInfoList(infoSet);
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                return INVALID_HANDLE_VALUE;
            }

            context->Signature = WINUSB_COMPAT_SIGNATURE;
            context->DeviceHandle = deviceHandle;
            context->InterfaceHandle = winusbHandle;
            context->PipeId = pipeId;
            context->IsInput = input;

            SetupDiDestroyDeviceInfoList(infoSet);
            return (HANDLE)context;
        }

        lastError = GetLastError();
        WinUsb_Free(winusbHandle);
        ::CloseHandle(deviceHandle);
        ++index;
    }

    SetupDiDestroyDeviceInfoList(infoSet);
    SetLastError(lastError);
    return INVALID_HANDLE_VALUE;
}

extern "C" HANDLE WinUsbCompatOpenBulkUSB(int output)
{
    const GUID* interfaceGuid =
        (g_deviceType == 0) ? &GUID_CLASS_DOCKING_SYSTEM : &GUID_CLASS_PRODUCTION_TEST;

    return OpenWinUsbPipe(interfaceGuid, output == 0);
}

extern "C" BOOL WinUsbCompatReadFile(
    HANDLE handle,
    LPVOID buffer,
    DWORD bytesToRead,
    LPDWORD bytesRead,
    LPOVERLAPPED overlapped)
{
    PWINUSB_COMPAT_CONTEXT context = (PWINUSB_COMPAT_CONTEXT)handle;

    if (handle == INVALID_HANDLE_VALUE ||
        !context ||
        context->Signature != WINUSB_COMPAT_SIGNATURE ||
        !context->IsInput) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    return WinUsb_ReadPipe(
        context->InterfaceHandle,
        context->PipeId,
        (PUCHAR)buffer,
        bytesToRead,
        (PULONG)bytesRead,
        overlapped);
}

extern "C" BOOL WinUsbCompatWriteFile(
    HANDLE handle,
    LPCVOID buffer,
    DWORD bytesToWrite,
    LPDWORD bytesWritten,
    LPOVERLAPPED overlapped)
{
    PWINUSB_COMPAT_CONTEXT context = (PWINUSB_COMPAT_CONTEXT)handle;

    if (handle == INVALID_HANDLE_VALUE ||
        !context ||
        context->Signature != WINUSB_COMPAT_SIGNATURE ||
        context->IsInput) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    return WinUsb_WritePipe(
        context->InterfaceHandle,
        context->PipeId,
        (PUCHAR)buffer,
        bytesToWrite,
        (PULONG)bytesWritten,
        overlapped);
}

extern "C" BOOL WinUsbCompatCloseHandle(HANDLE handle)
{
    PWINUSB_COMPAT_CONTEXT context = (PWINUSB_COMPAT_CONTEXT)handle;

    if (handle == INVALID_HANDLE_VALUE ||
        !context ||
        context->Signature != WINUSB_COMPAT_SIGNATURE) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    context->Signature = 0;
    WinUsb_Free(context->InterfaceHandle);
    ::CloseHandle(context->DeviceHandle);
    free(context);
    return TRUE;
}

extern "C" void WinUsbCompatChooseUSBDevice(int type)
{
    g_deviceType = type;
}

extern "C" void WinUsbCompatSelectCDASVersion(int version)
{
    g_cdasVersion = version;
}

extern "C" void WinUsbCompatChooseUSBMode(int mode)
{
    UNREFERENCED_PARAMETER(mode);
}

extern "C" int WinUsbCompatGetUSBDeviceNum(bool* foundCDAS1, bool* foundCDAS2)
{
    if (foundCDAS1) {
        *foundCDAS1 = false;
    }

    if (foundCDAS2) {
        *foundCDAS2 = false;
    }

    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return 0;
}

extern "C" bool WinUsbCompatIsSingleCDAS1Attached(void)
{
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return false;
}

extern "C" bool WinUsbCompatIsSingleCDAS2Attached(void)
{
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return false;
}
