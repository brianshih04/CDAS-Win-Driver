// cdas_cmd.cpp : CDAS command protocol implementation
//
// Implements the CDAS command protocol layer on top of the winusb_compat
// transport.  All CDAS command formatting, response verification, and
// high-level operations (serial number, image download, firmware update)
// are consolidated here.
#include "stdafx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "..\include\bulkusb_api.h"
#include "..\include\winusb_compat.h"
#include "..\include\cdas_cmd.h"

/* ── Internal device context ────────────────────────────────────── */

typedef struct _CDAS_DEVICE_CTX {
    HANDLE hRead;   /* bulk-in pipe  (compat context) */
    HANDLE hWrite;  /* bulk-out pipe (compat context) */
} CDAS_DEVICE_CTX;

/* ── Packet helpers ─────────────────────────────────────────────── */

int CdasSwapInt(int n)
{
    return ((n & 0xff000000) >> 24) |
           ((n & 0x00ff0000) >> 8)  |
           ((n & 0x0000ff00) << 8)  |
           ((n & 0x000000ff) << 24);
}

void CdasFillCmd(char* pbuf, int tag, char cmd, int startSector, short nbSector)
{
    int* pint;
    short* pshort;

    memset(pbuf, 0, CDAS_CMD_LENGTH);

    /* Magic "USBC" */
    pbuf[0] = 0x55;
    pbuf[1] = 0x53;
    pbuf[2] = 0x42;
    pbuf[3] = 0x43;

    /* Tag */
    pint = (int*)&pbuf[4];
    *pint = tag;

    /* Command */
    pbuf[8] = cmd;

    /* Start sector */
    pint = (int*)&pbuf[12];
    *pint = startSector;

    /* Sector count */
    pshort = (short*)&pbuf[16];
    *pshort = nbSector;
}

int CdasVerifyCmdStatus(const char* pbuf, int tag)
{
    int success = 1;
    const int* pint;

    if (pbuf[0] != 0x55) success = 0;
    if (pbuf[1] != 0x53) success = 0;
    if (pbuf[2] != 0x42) success = 0;
    if (pbuf[3] != 0x53) success = 0;

    pint = (const int*)&pbuf[4];
    if (*pint != tag) success = 0;

    return success;
}

/* ── Result code to string ──────────────────────────────────────── */

const char* CdasResultString(int result)
{
    switch (result) {
    case CDAS_OK:                      return "OK";
    case CDAS_ERR_PIPE_OPEN_IN:       return "Failed to open bulk-in pipe";
    case CDAS_ERR_PIPE_OPEN_OUT:      return "Failed to open bulk-out pipe";
    case CDAS_ERR_WRITE_CMD:          return "Failed to write command packet";
    case CDAS_ERR_READ_DATA:          return "Failed to read data";
    case CDAS_ERR_WRITE_DATA:         return "Failed to write data";
    case CDAS_ERR_READ_STATUS:        return "Failed to read status response";
    case CDAS_ERR_STATUS_MISMATCH:    return "Status response mismatch (bad magic or tag)";
    case CDAS_ERR_READ_SHORT:         return "Read fewer bytes than expected";
    case CDAS_ERR_WRITE_SHORT:        return "Wrote fewer bytes than expected";
    case CDAS_ERR_STATUS_SHORT:       return "Status response too short";
    case CDAS_ERR_WRITE_LEN_MISMATCH: return "Command write length mismatch";
    default:                          return "Unknown error";
    }
}

/* ── Open / Close ──────────────────────────────────────────────── */

HCDAS CdasOpen(int deviceType, int cdasVersion)
{
    CDAS_DEVICE_CTX* ctx;
    HANDLE hRead, hWrite;

    ChooseUSBDevice(deviceType);
    SelectCDASVersion(cdasVersion);

    hRead = OpenBulkUSB(0);  /* bulk-in */
    if (hRead == INVALID_HANDLE_VALUE) {
        return NULL;
    }

    hWrite = OpenBulkUSB(1); /* bulk-out */
    if (hWrite == INVALID_HANDLE_VALUE) {
        CloseHandle(hRead);
        return NULL;
    }

    ctx = (CDAS_DEVICE_CTX*)malloc(sizeof(CDAS_DEVICE_CTX));
    if (!ctx) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return NULL;
    }

    ctx->hRead = hRead;
    ctx->hWrite = hWrite;
    return (HCDAS)ctx;
}

void CdasClose(HCDAS ctx)
{
    if (!ctx) return;

    CDAS_DEVICE_CTX* c = (CDAS_DEVICE_CTX*)ctx;
    if (c->hRead != INVALID_HANDLE_VALUE) {
        CloseHandle(c->hRead);
    }
    if (c->hWrite != INVALID_HANDLE_VALUE) {
        CloseHandle(c->hWrite);
    }
    free(c);
}

/* ── Low-level commands ─────────────────────────────────────────── */

int CdasIssueCommand(
    HCDAS ctx,
    char cmd,
    int input,
    int startSector,
    short nbSector,
    char* buffer)
{
    CDAS_DEVICE_CTX* c = (CDAS_DEVICE_CTX*)ctx;
    char cmd_buf[64], response_buf[64];
    ULONG writeLen, nBytesWrite, readLen, nBytesRead;
    BOOL success;
    int tag;

    srand((unsigned int)time(NULL));
    tag = rand();

    CdasFillCmd(cmd_buf, tag, cmd, startSector, nbSector);

    writeLen = CDAS_CMD_LENGTH;
    success = WriteFile(c->hWrite, cmd_buf, writeLen, &nBytesWrite, NULL);
    if (!success) {
        return CDAS_ERR_WRITE_CMD;
    }
    if (nBytesWrite != writeLen) {
        return CDAS_ERR_WRITE_LEN_MISMATCH;
    }

    if (nbSector && input && buffer != NULL) {
        success = ReadFile(c->hRead, buffer, 512 * nbSector, &nBytesRead, NULL);
        if (!success) return CDAS_ERR_READ_DATA;
        if (nBytesRead != (ULONG)(512 * nbSector)) return CDAS_ERR_READ_SHORT;
    }
    else if (nbSector && buffer != NULL) {
        success = WriteFile(c->hWrite, buffer, 512 * nbSector, &nBytesRead, NULL);
        if (!success) return CDAS_ERR_WRITE_DATA;
        if (nBytesRead != (ULONG)(512 * nbSector)) return CDAS_ERR_WRITE_SHORT;
    }

    readLen = 32;
    success = ReadFile(c->hRead, response_buf, readLen, &nBytesRead, NULL);
    if (!success) return CDAS_ERR_READ_STATUS;
    if (nBytesRead != readLen) return CDAS_ERR_STATUS_SHORT;

    if (!CdasVerifyCmdStatus(response_buf, tag)) {
        return CDAS_ERR_STATUS_MISMATCH;
    }

    return CDAS_OK;
}

int CdasIssueCommand2(
    HCDAS ctx,
    char cmd,
    int input,
    int nbBytes,
    char* buffer)
{
    CDAS_DEVICE_CTX* c = (CDAS_DEVICE_CTX*)ctx;
    char cmd_buf[64], response_buf[64];
    ULONG writeLen, nBytesWrite, readLen, nBytesRead;
    BOOL success;
    int tag;

    srand((unsigned int)time(NULL));
    tag = rand();

    CdasFillCmd(cmd_buf, tag, cmd, 0, 0);

    writeLen = CDAS_CMD_LENGTH;
    success = WriteFile(c->hWrite, cmd_buf, writeLen, &nBytesWrite, NULL);
    if (!success) return CDAS_ERR_WRITE_CMD;
    if (nBytesWrite != writeLen) return CDAS_ERR_WRITE_LEN_MISMATCH;

    if (nbBytes && input && buffer != NULL) {
        success = ReadFile(c->hRead, buffer, nbBytes, &nBytesRead, NULL);
        if (!success) return CDAS_ERR_READ_DATA;
        if (nBytesRead != (ULONG)nbBytes) return CDAS_ERR_READ_SHORT;
    }
    else if (nbBytes && buffer != NULL) {
        success = WriteFile(c->hWrite, buffer, nbBytes, &nBytesRead, NULL);
        if (!success) return CDAS_ERR_WRITE_DATA;
        if (nBytesRead != (ULONG)nbBytes) return CDAS_ERR_WRITE_SHORT;
    }

    readLen = 32;
    success = ReadFile(c->hRead, response_buf, readLen, &nBytesRead, NULL);
    if (!success) return CDAS_ERR_READ_STATUS;
    if (nBytesRead != readLen) return CDAS_ERR_STATUS_SHORT;

    if (!CdasVerifyCmdStatus(response_buf, tag)) {
        return CDAS_ERR_STATUS_MISMATCH;
    }

    return CDAS_OK;
}

/* ── High-level convenience ────────────────────────────────────── */

int CdasReadSerialNumber(HCDAS ctx, char* buffer)
{
    return CdasIssueCommand2(ctx, CDAS_CMD_READ_SERIAL_NUMBER, 1, 16, buffer);
}

int CdasWriteSerialNumber(HCDAS ctx, const char* buffer)
{
    /* buffer is a 32-byte region (null-terminated string, zero-padded) */
    char tmp[64];
    memset(tmp, 0, sizeof(tmp));
    if (buffer) {
        strncpy(tmp, buffer, 31);
        tmp[31] = '\0';
    }
    return CdasIssueCommand2(ctx, CDAS_CMD_WRITE_SERIAL_NUMBER, 0, 32, tmp);
}

int CdasReadImage(
    HCDAS ctx,
    int option,
    const char* filename,
    CDAS_PROGRESS_CALLBACK progressCb,
    void* progressCtx,
    CDAS_LOG_CALLBACK logCb,
    void* logCtx)
{
    char* pBuff;
    int i, total_sector;
    FILE* pFile;
    int cmd;
    int success;

    UNREFERENCED_PARAMETER(ctx);

    pFile = fopen(filename, "wb");
    if (!pFile) {
        return CDAS_ERR_READ_DATA;
    }

    pBuff = (char*)malloc(CDAS_SECTOR_PER_READ * 512);
    if (!pBuff) {
        fclose(pFile);
        return CDAS_ERR_READ_DATA;
    }

    if (option == 2) {
        cmd = CDAS_CMD_UPLOAD_IMAGE2;
    }
    else if (option == 1) {
        cmd = CDAS_CMD_UPLOAD_RIGI_TEST;
    }
    else {
        cmd = CDAS_CMD_UPLOAD_IMAGE;
    }

    success = CdasIssueCommand(ctx, (char)cmd, 1, 0, 0, NULL);
    if (success != CDAS_OK) {
        free(pBuff);
        fclose(pFile);
        return success;
    }

    i = 0;
    total_sector = -1;

    if (logCb) logCb("Start image data upload ...", logCtx);

    while (total_sector != 0) {
        success = CdasIssueCommand(
            ctx, CDAS_CMD_UPLOADING, 1,
            CDAS_IMAGE_SECTOR_START + i, CDAS_SECTOR_PER_READ, pBuff);

        if (success == CDAS_OK) {
            if (total_sector == -1) {
                if (option == 2) {
                    total_sector = CdasSwapInt(*(int*)(pBuff + 4));
                }
                else {
                    total_sector = CdasSwapInt(*(int*)pBuff);
                }
                if (total_sector == 0xFFFFFFFF) {
                    total_sector = CDAS_ESTIMATE_MAX_PAGE;
                }
            }

            if (total_sector > CDAS_SECTOR_PER_READ) {
                fwrite(pBuff, 512, CDAS_SECTOR_PER_READ, pFile);
                total_sector -= CDAS_SECTOR_PER_READ;
                i += CDAS_SECTOR_PER_READ;
            }
            else {
                fwrite(pBuff, 512, total_sector, pFile);
                i += total_sector;
                total_sector = 0;
            }

            if (progressCb) {
                progressCb(i, i + (total_sector > 0 ? total_sector : 0), progressCtx);
            }
        }
        else {
            if (logCb) logCb("Can't read image data", logCtx);
            CdasIssueCommand(ctx, CDAS_CMD_UPLOAD_END, 0, 0, 0, NULL);
            free(pBuff);
            fclose(pFile);
            return success;
        }
    }

    CdasIssueCommand(ctx, CDAS_CMD_UPLOAD_END, 0, 0, 0, NULL);
    fclose(pFile);
    free(pBuff);

    if (logCb) logCb("Image download complete.", logCtx);
    return CDAS_OK;
}

int CdasUpdateFirmware(
    HCDAS ctx,
    char fwType,
    const char* filename,
    CDAS_PROGRESS_CALLBACK progressCb,
    void* progressCtx,
    CDAS_LOG_CALLBACK logCb,
    void* logCtx)
{
    char* pBuf;
    FILE* pFile;
    size_t bytesRead;
    int success;
    char msg[256];

    pBuf = (char*)malloc(CDAS_FIRMWARE_SIZE);
    if (!pBuf) return CDAS_ERR_READ_DATA;

    pFile = fopen(filename, "rb");
    if (!pFile) {
        free(pBuf);
        return CDAS_ERR_READ_DATA;
    }

    bytesRead = fread(pBuf, 1, CDAS_FIRMWARE_SIZE, pFile);
    fclose(pFile);

    if (bytesRead != CDAS_FIRMWARE_SIZE) {
        if (logCb) {
            _snprintf(msg, sizeof(msg),
                "Warning: firmware file size (%lu) != expected (%d)",
                (unsigned long)bytesRead, CDAS_FIRMWARE_SIZE);
            logCb(msg, logCtx);
        }
    }

    success = CdasIssueCommand(
        ctx, fwType, 0, 0, CDAS_FIRMWARE_SIZE / 512, pBuf);

    if (success == CDAS_OK) {
        if (logCb) logCb("Firmware update completed successfully.", logCtx);
        if (progressCb) progressCb(1, 1, progressCtx);
    }
    else {
        if (logCb) {
            _snprintf(msg, sizeof(msg),
                "Firmware update failed: %s", CdasResultString(success));
            logCb(msg, logCtx);
        }
    }

    free(pBuf);
    return success;
}

/* ── Enumerate devices ──────────────────────────────────────────── */

int CdasEnumerateDevices(void)
{
    bool found1 = false, found2 = false;
    int count = 0;

    /* Try CDAS1 */
    SelectCDASVersion(CDAS_VERSION_1);
    ChooseUSBDevice(CDAS_DEVICE_DOCKING);
    HANDLE h = OpenBulkUSB(0);
    if (h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
        found1 = true;
        count++;
    }

    /* Try CDAS2 */
    SelectCDASVersion(CDAS_VERSION_2);
    ChooseUSBDevice(CDAS_DEVICE_DOCKING);
    h = OpenBulkUSB(0);
    if (h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
        found2 = true;
        count++;
    }

    /* Try any (production test) */
    SelectCDASVersion(CDAS_VERSION_ANY);
    ChooseUSBDevice(CDAS_DEVICE_PRODUCTION_TEST);
    h = OpenBulkUSB(0);
    if (h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
        count++;
    }

    /* Reset to defaults */
    SelectCDASVersion(CDAS_VERSION_ANY);
    ChooseUSBDevice(CDAS_DEVICE_DOCKING);

    return count;
}
