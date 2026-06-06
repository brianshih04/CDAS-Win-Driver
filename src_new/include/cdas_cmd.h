#ifndef __CDAS_CMD_H__
#define __CDAS_CMD_H__

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── CDAS command byte constants ────────────────────────────────── */

#define CDAS_CMD_UPDATE_FW_BOOT       0x01
#define CDAS_CMD_UPDATE_FW_CORE       0x02
#define CDAS_CMD_UPDATE_FW_CAM        0x03
#define CDAS_CMD_UPLOAD_IMAGE         0x11
#define CDAS_CMD_UPLOAD_RIGI_TEST     0x12
#define CDAS_CMD_UPLOAD_IMAGE2        0x13
#define CDAS_CMD_UPLOADING            0x18
#define CDAS_CMD_UPLOAD_END           0x19
#define CDAS_CMD_WRITE_SERIAL_NUMBER  0x21
#define CDAS_CMD_READ_SERIAL_NUMBER   0x22

#define CDAS_CMD_LENGTH               64

/* ── Flash geometry ──────────────────────────────────────────────── */

#define CDAS_FIRMWARE_SIZE            (24 * 512)
#define CDAS_FIRMWARE_SECTOR_START    2036
#define CDAS_IMAGE_SECTOR_START       2060
#define CDAS_SECTOR_PER_READ          32

#define CDAS_FLASH_2048              1
#if CDAS_FLASH_2048
#define CDAS_ESTIMATE_MAX_PAGE        0x1FF7A
#define CDAS_BYTES_PER_PAGE           2048
#else
#define CDAS_ESTIMATE_MAX_PAGE        0x3FC00
#define CDAS_BYTES_PER_PAGE           512
#endif

/* ── Device version ────────────────────────────────────────────── */

#define CDAS_VERSION_ANY  0
#define CDAS_VERSION_1    1
#define CDAS_VERSION_2    2

/* ── Device type ────────────────────────────────────────────────── */

#define CDAS_DEVICE_DOCKING         0
#define CDAS_DEVICE_PRODUCTION_TEST 1

/* ── Callback types ─────────────────────────────────────────────── */

typedef void (__stdcall *CDAS_PROGRESS_CALLBACK)(
    int currentPage,
    int totalPages,
    void* context);

typedef void (__stdcall *CDAS_LOG_CALLBACK)(
    const char* message,
    void* context);

/* ── Error codes returned by CdasIssueCommand / CdasIssueCommand2 ─ */

#define CDAS_OK                      1
#define CDAS_ERR_PIPE_OPEN_IN       -100
#define CDAS_ERR_PIPE_OPEN_OUT      -101
#define CDAS_ERR_WRITE_CMD          -102
#define CDAS_ERR_READ_DATA          -103
#define CDAS_ERR_WRITE_DATA         -104
#define CDAS_ERR_READ_STATUS        -105
#define CDAS_ERR_STATUS_MISMATCH    -4
#define CDAS_ERR_READ_SHORT         -1
#define CDAS_ERR_WRITE_SHORT        -2
#define CDAS_ERR_STATUS_SHORT       -3
#define CDAS_ERR_WRITE_LEN_MISMATCH -5

/* ── Opaque device context ──────────────────────────────────────── */

typedef struct _CDAS_DEVICE_CTX* HCDAS;

/* ── Enumerate devices ──────────────────────────────────────────── */

/* Returns number of CDAS devices found. */
int CdasEnumerateDevices(void);

/* ── Open / Close ───────────────────────────────────────────────── */

/*
   Opens bulk-in and bulk-out pipes for the first matching CDAS device.
   Pass deviceType (0=docking, 1=production test) and cdasVersion
   (0=any, 1=CDAS1, 2=CDAS2).
   Returns HCDAS on success, NULL on failure.
 */
HCDAS CdasOpen(int deviceType, int cdasVersion);

void CdasClose(HCDAS ctx);

/* ── Low-level commands ─────────────────────────────────────────── */

/*
   Sector-based command (512-byte units).
   cmd:     command byte
   input:   1 = device→host read, 0 = host→device write
   startSector / nbSector: sector arguments
   buffer:  data buffer (may be NULL if nbSector==0)
   Returns CDAS_OK on success, negative on failure.
 */
int CdasIssueCommand(
    HCDAS ctx,
    char cmd,
    int input,
    int startSector,
    short nbSector,
    char* buffer);

/*
   Byte-based command.
   cmd:     command byte
   input:   1 = read, 0 = write
   nbBytes: number of bytes to transfer
   buffer:  data buffer (may be NULL if nbBytes==0)
 */
int CdasIssueCommand2(
    HCDAS ctx,
    char cmd,
    int input,
    int nbBytes,
    char* buffer);

/* ── High-level convenience functions ────────────────────────────── */

/* Read serial number into buffer (16 bytes). Returns CDAS_OK or error. */
int CdasReadSerialNumber(HCDAS ctx, char* buffer);

/* Write serial number from buffer (32 bytes, null-terminated string). */
int CdasWriteSerialNumber(HCDAS ctx, const char* buffer);

/*
   Download image data from capsule to file.
   option:  0 = upload image only
            1 = rigi-flex test + upload
            2 = fast GPIO link upload
   filename: output file path
   progressCb: optional progress callback (may be NULL)
   progressCtx: context pointer passed to progressCb
   logCb: optional log callback (may be NULL)
   logCtx: context pointer passed to logCb
   Returns CDAS_OK on success.
 */
int CdasReadImage(
    HCDAS ctx,
    int option,
    const char* filename,
    CDAS_PROGRESS_CALLBACK progressCb,
    void* progressCtx,
    CDAS_LOG_CALLBACK logCb,
    void* logCtx);

/*
   Update firmware on device.
   fwType:  CDAS_CMD_UPDATE_FW_BOOT / _CORE / _CAM
   filename: path to .bin firmware file
   progressCb: optional progress callback (may be NULL)
   progressCtx: context pointer passed to progressCb
   logCb: optional log callback (may be NULL)
   logCtx: context pointer passed to logCb
   Returns CDAS_OK on success.
 */
int CdasUpdateFirmware(
    HCDAS ctx,
    char fwType,
    const char* filename,
    CDAS_PROGRESS_CALLBACK progressCb,
    void* progressCtx,
    CDAS_LOG_CALLBACK logCb,
    void* logCtx);

/* ── Packet helpers (exposed for raw-command use) ────────────────── */

void CdasFillCmd(char* pbuf, int tag, char cmd, int startSector, short nbSector);
int  CdasVerifyCmdStatus(const char* pbuf, int tag);
int  CdasSwapInt(int n);

/* ── Result code to string ───────────────────────────────────────── */

const char* CdasResultString(int result);

#ifdef __cplusplus
}
#endif

#endif /* __CDAS_CMD_H__ */
