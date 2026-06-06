/*
   CDAS WinUSB direct sample.

   This sample intentionally does not use winusb_compat.h or the
   bulkusb_lib-2010 wrapper project. It keeps the same command flow as
   capsousb_test.cpp, but opens the WinUSB device interface and calls
   WinUsb_ReadPipe/WinUsb_WritePipe directly.
 */

#include <windows.h>
#include <setupapi.h>
#include <winusb.h>
#include <usb.h>
#include <tchar.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "winusb.lib")

static const GUID GUID_CLASS_DOCKING_SYSTEM =
    {0x873fdf, 0x62a8, 0x11d1, {0xaa, 0x5e, 0x00, 0xc0, 0x4f, 0xb1, 0x72, 0x8b}};

static const GUID GUID_CLASS_PRODUCTION_TEST =
    {0xc20e4f22, 0x44b8, 0x4feb, {0x8f, 0x31, 0xf7, 0x8e, 0x29, 0xcc, 0xd0, 0x35}};

#define CDAS1_VID_PID _T("USB\\VID_03EB&PID_941C")
#define CDAS2_VID_PID _T("USB\\VID_0638&PID_0931")
#define CDAS_PRODUCTION_TEST_VID_PID _T("USB\\VID_03EB&PID_952C")

#define FIRMWARE_SIZE         24 * 512
#define FIRMWARE_SECTOR_START 2036
#define IMAGE_SECTOR_START    2060
#define CMD_LENGTH            64
#define CMD_UPDATE_FW_BOOT          0x1
#define CMD_UPDATE_FW_CORE          0x2
#define CMD_UPDATE_FW_CAM           0x3
#define CMD_UPLOAD_IMAGE            0x11
#define CMD_UPLOAD_RIGI_TEST        0x12
#define CMD_UPLOAD_IMAGE2           0x13
#define CMD_UPLOADING               0x18
#define CMD_UPLOAD_END              0x19
#define CMD_WRITE_SERIAL_NUMBER     0x21
#define CMD_READ_SERIAL_NUMBER      0x22

#define FLASH_2048  1
#ifdef FLASH_2048
#define ESTIMATE_MAX_PAGE     0x1FF7A
#define BYTES_PER_PAGE        2048
#else
#define ESTIMATE_MAX_PAGE     0x3FC00
#define BYTES_PER_PAGE        512
#endif

typedef struct _DIRECT_WINUSB_PIPE {
    HANDLE DeviceHandle;
    WINUSB_INTERFACE_HANDLE InterfaceHandle;
    UCHAR PipeId;
    BOOL IsInput;
} DIRECT_WINUSB_PIPE, *PDIRECT_WINUSB_PIPE;

static int g_deviceType = 0;
static int g_cdasVersion = 0;

static int SwapInt(int n)
{
    return ((n & 0xff000000) >> 24) |
           ((n & 0x00ff0000) >> 8) |
           ((n & 0x0000ff00) << 8) |
           ((n & 0x000000ff) << 24);
}

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

static PDIRECT_WINUSB_PIPE OpenDirectWinUsbPipe(const GUID* interfaceGuid, BOOL input)
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
        return NULL;
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
            CloseHandle(deviceHandle);
            ++index;
            continue;
        }

        if (!WinUsb_Initialize(deviceHandle, &winusbHandle)) {
            lastError = GetLastError();
            CloseHandle(deviceHandle);
            ++index;
            continue;
        }

        if (SelectPipe(winusbHandle, input, &pipeId)) {
            PDIRECT_WINUSB_PIPE pipe =
                (PDIRECT_WINUSB_PIPE)malloc(sizeof(DIRECT_WINUSB_PIPE));

            if (!pipe) {
                WinUsb_Free(winusbHandle);
                CloseHandle(deviceHandle);
                SetupDiDestroyDeviceInfoList(infoSet);
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                return NULL;
            }

            pipe->DeviceHandle = deviceHandle;
            pipe->InterfaceHandle = winusbHandle;
            pipe->PipeId = pipeId;
            pipe->IsInput = input;

            SetupDiDestroyDeviceInfoList(infoSet);
            return pipe;
        }

        lastError = GetLastError();
        WinUsb_Free(winusbHandle);
        CloseHandle(deviceHandle);
        ++index;
    }

    SetupDiDestroyDeviceInfoList(infoSet);
    SetLastError(lastError);
    return NULL;
}

static PDIRECT_WINUSB_PIPE OpenDirectBulkPipe(BOOL input)
{
    const GUID* interfaceGuid =
        (g_deviceType == 0) ? &GUID_CLASS_DOCKING_SYSTEM : &GUID_CLASS_PRODUCTION_TEST;

    return OpenDirectWinUsbPipe(interfaceGuid, input);
}

static BOOL DirectReadPipe(
    PDIRECT_WINUSB_PIPE pipe,
    PVOID buffer,
    ULONG bytesToRead,
    PULONG bytesRead)
{
    if (!pipe || !pipe->IsInput) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    return WinUsb_ReadPipe(
        pipe->InterfaceHandle,
        pipe->PipeId,
        (PUCHAR)buffer,
        bytesToRead,
        bytesRead,
        NULL);
}

static BOOL DirectWritePipe(
    PDIRECT_WINUSB_PIPE pipe,
    const VOID* buffer,
    ULONG bytesToWrite,
    PULONG bytesWritten)
{
    if (!pipe || pipe->IsInput) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    return WinUsb_WritePipe(
        pipe->InterfaceHandle,
        pipe->PipeId,
        (PUCHAR)buffer,
        bytesToWrite,
        bytesWritten,
        NULL);
}

static void CloseDirectPipe(PDIRECT_WINUSB_PIPE pipe)
{
    if (!pipe) {
        return;
    }

    WinUsb_Free(pipe->InterfaceHandle);
    CloseHandle(pipe->DeviceHandle);
    free(pipe);
}

static void PrintLastError(const char* action)
{
    DWORD error = GetLastError();
    printf("%s failed. GetLastError=%lu (0x%08lX)\n", action, error, error);
}

static void PrintOpenDeviceError(const char* pipeName)
{
    DWORD error = GetLastError();

    if (error == ERROR_NOT_FOUND) {
        printf("CDAS WinUSB device not found while opening %s.\n", pipeName);
        printf("Please confirm the device is connected, the WinUSB INF is installed, and Device Manager shows winusb.sys.\n");
        return;
    }

    printf("OpenDirectBulkPipe %s failed. GetLastError=%lu (0x%08lX)\n", pipeName, error, error);
}

static void FillCmd(char* pbuf, int tag, char cmd, int start_sector, short nb_sector)
{
    int* pint;
    short* pshort;

    memset((void*)pbuf, 0, CMD_LENGTH);

    pbuf[0] = 0x55;
    pbuf[1] = 0x53;
    pbuf[2] = 0x42;
    pbuf[3] = 0x43;

    pint = (int*)&pbuf[4];
    *pint = tag;

    pbuf[8] = cmd;

    pint = (int*)&pbuf[12];
    *pint = start_sector;

    pshort = (short*)&pbuf[16];
    *pshort = nb_sector;
}

static int VerifyCmdStatus(char* pbuf, int tag)
{
    int success = 1;
    int* pint;

    if (pbuf[0] != 0x55) {
        success = 0;
    }
    if (pbuf[1] != 0x53) {
        success = 0;
    }
    if (pbuf[2] != 0x42) {
        success = 0;
    }
    if (pbuf[3] != 0x53) {
        success = 0;
    }

    pint = (int*)&pbuf[4];
    if (*pint != tag) {
        success = 0;
    }

    return success;
}

static int IssueCommand(char cmd, int input, int start_sector, short nb_sector, char* buffer)
{
    char cmd_buf[64], response_buf[64];
    ULONG writeLen, nBytesWrite, readLen, nBytesRead;
    int success = 1;
    int result = 1;
    PDIRECT_WINUSB_PIPE readPipe = NULL;
    PDIRECT_WINUSB_PIPE writePipe = NULL;
    int tag;

    srand((unsigned int)time(NULL));
    tag = rand();

    readPipe = OpenDirectBulkPipe(TRUE);
    if (!readPipe) {
        PrintOpenDeviceError("bulk-in");
        result = -100;
        goto cleanup;
    }

    writePipe = OpenDirectBulkPipe(FALSE);
    if (!writePipe) {
        PrintOpenDeviceError("bulk-out");
        result = -101;
        goto cleanup;
    }

    FillCmd(cmd_buf, tag, cmd, start_sector, nb_sector);
    writeLen = CMD_LENGTH;
    success = DirectWritePipe(writePipe, cmd_buf, writeLen, &nBytesWrite);
    if (!success) {
        PrintLastError("WinUsb_WritePipe command");
        result = -102;
        goto cleanup;
    }
    if (nBytesWrite != writeLen) {
        result = -5;
        goto cleanup;
    }

    if (nb_sector && input && buffer != NULL) {
        success = DirectReadPipe(readPipe, buffer, 512 * nb_sector, &nBytesRead);
        if (!success) {
            PrintLastError("WinUsb_ReadPipe data");
            result = -103;
            goto cleanup;
        }
        if (nBytesRead != 512 * nb_sector) {
            result = -1;
            goto cleanup;
        }
    }
    else if (nb_sector && buffer != NULL) {
        success = DirectWritePipe(writePipe, buffer, 512 * nb_sector, &nBytesRead);
        if (!success) {
            PrintLastError("WinUsb_WritePipe data");
            result = -104;
            goto cleanup;
        }
        if (nBytesRead != 512 * nb_sector) {
            result = -2;
            goto cleanup;
        }
    }

    readLen = 32;
    success = DirectReadPipe(readPipe, response_buf, readLen, &nBytesRead);
    if (!success) {
        PrintLastError("WinUsb_ReadPipe command status");
        result = -105;
        goto cleanup;
    }
    if (nBytesRead != readLen) {
        result = -3;
        goto cleanup;
    }

    success = VerifyCmdStatus(response_buf, tag);
    if (!success) {
        printf("Sector Read error.\n");
        result = -4;
        goto cleanup;
    }

    result = success;

cleanup:
    CloseDirectPipe(readPipe);
    CloseDirectPipe(writePipe);
    return result;
}

static int IssueCommand2(char cmd, int input, int nb_bytes, char* buffer)
{
    char cmd_buf[64], response_buf[64];
    ULONG writeLen, nBytesWrite, readLen, nBytesRead;
    int success = 1;
    int result = 1;
    PDIRECT_WINUSB_PIPE readPipe = NULL;
    PDIRECT_WINUSB_PIPE writePipe = NULL;
    int tag;

    srand((unsigned int)time(NULL));
    tag = rand();

    readPipe = OpenDirectBulkPipe(TRUE);
    if (!readPipe) {
        PrintOpenDeviceError("bulk-in");
        result = -100;
        goto cleanup;
    }

    writePipe = OpenDirectBulkPipe(FALSE);
    if (!writePipe) {
        PrintOpenDeviceError("bulk-out");
        result = -101;
        goto cleanup;
    }

    FillCmd(cmd_buf, tag, cmd, 0, 0);
    writeLen = CMD_LENGTH;
    success = DirectWritePipe(writePipe, cmd_buf, writeLen, &nBytesWrite);
    if (!success) {
        PrintLastError("WinUsb_WritePipe command");
        result = -102;
        goto cleanup;
    }
    if (nBytesWrite != writeLen) {
        result = -5;
        goto cleanup;
    }

    if (nb_bytes && input && buffer != NULL) {
        success = DirectReadPipe(readPipe, buffer, nb_bytes, &nBytesRead);
        if (!success) {
            PrintLastError("WinUsb_ReadPipe data");
            result = -103;
            goto cleanup;
        }
        if (nBytesRead != (ULONG)nb_bytes) {
            result = -1;
            goto cleanup;
        }
    }
    else if (nb_bytes && buffer != NULL) {
        success = DirectWritePipe(writePipe, buffer, nb_bytes, &nBytesRead);
        if (!success) {
            PrintLastError("WinUsb_WritePipe data");
            result = -104;
            goto cleanup;
        }
        if (nBytesRead != (ULONG)nb_bytes) {
            result = -2;
            goto cleanup;
        }
    }

    readLen = 32;
    success = DirectReadPipe(readPipe, response_buf, readLen, &nBytesRead);
    if (!success) {
        PrintLastError("WinUsb_ReadPipe command status");
        result = -105;
        goto cleanup;
    }
    if (nBytesRead != readLen) {
        result = -3;
        goto cleanup;
    }

    success = VerifyCmdStatus(response_buf, tag);
    if (!success) {
        printf("Sector Read error.\n");
        result = -4;
        goto cleanup;
    }

    result = success;

cleanup:
    CloseDirectPipe(readPipe);
    CloseDirectPipe(writePipe);
    return result;
}

#define SECTOR_PER_READ       32

static void ReadImageFromDockingSystem(char* filename, int option)
{
    char* pBuff;
    int i, total_sector;
    FILE* pFile;
    int success = 1;

    pFile = fopen(filename, "wb");
    if (!pFile) {
        PrintLastError("fopen image output");
        return;
    }

    i = 0;
    total_sector = -1;
    pBuff = (char*)malloc(SECTOR_PER_READ * 512);
    if (!pBuff) {
        fclose(pFile);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        PrintLastError("malloc image buffer");
        return;
    }

    if (option == 2) {
        success = IssueCommand(CMD_UPLOAD_IMAGE2, 1, 0, 0, NULL);
    }
    else if (option == 1) {
        success = IssueCommand(CMD_UPLOAD_RIGI_TEST, 1, 0, 0, NULL);
    }
    else {
        success = IssueCommand(CMD_UPLOAD_IMAGE, 1, 0, 0, NULL);
    }

    if (!success) {
        free(pBuff);
        fclose(pFile);
        return;
    }

    printf("Start image data upload ...\n");
    while (total_sector) {
        if (IssueCommand(CMD_UPLOADING, 1, IMAGE_SECTOR_START + i, SECTOR_PER_READ, pBuff) == TRUE) {
            if (total_sector == -1) {
                if (option == 2) {
                    total_sector = SwapInt(*(int*)(pBuff + 4));
                }
                else {
                    total_sector = SwapInt(*(int*)pBuff);
                }

                if (total_sector == 0xFFFFFFFF) {
                    total_sector = ESTIMATE_MAX_PAGE;
                }
                printf("total pages = %d\n", total_sector);
            }

            if (total_sector > SECTOR_PER_READ) {
                fwrite(pBuff, 512, SECTOR_PER_READ, pFile);
                total_sector -= SECTOR_PER_READ;
                i += SECTOR_PER_READ;
            }
            else {
                fwrite(pBuff, 512, total_sector, pFile);
                i += total_sector;
                total_sector = 0;
            }
            printf("page = %d\r", i);
        }
        else {
            printf("Can't read image data");
            break;
        }
    }

    IssueCommand(CMD_UPLOAD_END, 0, 0, 0, NULL);
    fclose(pFile);
    free(pBuff);
}

int _cdecl main(int argc, char* argv[])
{
    char serial_num[] = "CV090123-1234567";
    char buffer[64];
    int result;

    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);

    result = IssueCommand2(CMD_READ_SERIAL_NUMBER, 1, 16, buffer);
    if (result != 1) {
        printf("Read serial number failed: %d\n", result);
        return result;
    }

    strcpy(buffer, serial_num);
    result = IssueCommand2(CMD_WRITE_SERIAL_NUMBER, 0, 32, buffer);
    if (result != 1) {
        printf("Write serial number failed: %d\n", result);
        return result;
    }

    result = IssueCommand2(CMD_READ_SERIAL_NUMBER, 1, 16, buffer);
    if (result != 1) {
        printf("Read serial number failed: %d\n", result);
        return result;
    }

    return 0;
}
