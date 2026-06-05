// capsodrv.cpp : Defines the entry point for the console application.
//
#include "stdafx.h"
/*++

   Copyright (c) 1997-1998  Microsoft Corporation

   Module Name:

    RWBulk.c

   Abstract:

    Console test app for BulkUsb.sys driver

   Environment:

    user mode only

   Notes:

   THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY
   KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
   IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR
   PURPOSE.

   Copyright (c) 1997-1998 Microsoft Corporation.  All Rights Reserved.


   Revision History:

        11/17/97: created

   --*/
#include <windows.h>
#include <conio.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#include "..\\include\\BulkUsb_API.h"
#include "..\\include\\winusb_compat.h"
static int SwapInt(int n) {
    int t;
    t = ( (n & 0xff000000) >> 24 ) | ( (n & 0xff0000) >> 8 ) | ( (n & 0xff00) << 8 ) | ( (n & 0xff) << 24 );
    return t;
}
#define FIRMWARE_SIZE         24 * 512    // 24 pages with 512 bytes per page
#define FIRMWARE_SECTOR_START 2036
#define IMAGE_SECTOR_START    2060
#define CMD_LENGTH            64
#define CMD_UPDATE_FW_BOOT          0x1     /*update capsoboot*/
#define CMD_UPDATE_FW_CORE          0x2     /*update capsocore*/
#define CMD_UPDATE_FW_CAM           0x3     /*update capsocam*/
#define CMD_UPLOAD_IMAGE            0x11    /*upload picture data*/
#define CMD_UPLOAD_RIGI_TEST        0x12    /*rigi-flex board test followed by upload image data*/
#define CMD_UPLOAD_IMAGE2           0x13    /*upload image data through fast GPIO link (for test fixture use)*/
#define CMD_UPLOADING               0x18    /*upload the real data*/
#define CMD_UPLOAD_END              0x19    /*stop uploading*/
#define CMD_WRITE_SERIAL_NUMBER     0x21
#define CMD_READ_SERIAL_NUMBER      0x22
#define FLASH_2048  1
#ifdef FLASH_2048
  #define ESTIMATE_MAX_PAGE     0x1FF7A     // (2048-2)*64=0x1ff80, total blocks(2048) - 2 blocks, 64 pages per block
                                            // also give some margin for the bad block inside both docking system
                                            // and the capsule
  #define BYTES_PER_PAGE        2048
#else
  #define ESTIMATE_MAX_PAGE         0x3FC00       /*total blocks (8192) - 32 blocks, defined in system.h*/
  #define BYTES_PER_PAGE        512
#endif
void FillCmd(char* pbuf, int tag, char cmd, int start_sector, short nb_sector) {
    int* pint;
    short* pshort;
    memset( (void*)pbuf, 0, CMD_LENGTH );
    /*"USBC", 4 bytes*/
    pbuf[0] = 0x55;
    pbuf[1] = 0x53;
    pbuf[2] = 0x42;
    pbuf[3] = 0x43;
    /*tag, 4 bytes*/
    pint = (int*)&pbuf[4];
    * pint = tag;
    /*cmd, 4 bytes*/
    pbuf[8] = cmd;
    /*sectors to transfer, 4 bytes*/
    pint = (int*)&pbuf[12];
    * pint = start_sector;
    /*sectors to transfer, 2 bytes*/
    pshort = (short*)&pbuf[16];
    * pshort = nb_sector;
}
int VerifyCmdStatus(char* pbuf, int tag) {
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
    if (* pint  != tag) {
        success = 0;
    }
    return success;
}
/*sector 512 based command*/
int IssueCommand(char cmd, int input, int start_sector, short nb_sector, char* buffer) {
    char cmd_buf[64], response_buf[64];
    ULONG WriteLen, nBytesWrite, ReadLen, nBytesRead;
    int success = 1;
    HANDLE hRead = INVALID_HANDLE_VALUE, hWrite = INVALID_HANDLE_VALUE;
    int tag;
    srand( static_cast<unsigned int>( time(NULL) ) );
    tag = rand();
    hRead = OpenBulkUSB(0);     // bulk-in
    if (hRead == INVALID_HANDLE_VALUE) {
        assert(0);
        return -100;
    }
    hWrite = OpenBulkUSB(1);    // bulk-out
    if (hWrite == INVALID_HANDLE_VALUE) {
        assert(0);
        return -101;
    }
    /* Issue the command */
    FillCmd(cmd_buf, tag, cmd, start_sector, nb_sector);  /*start_sector=0, 23x512=12K data to transfer*/
    WriteLen = CMD_LENGTH;
    WriteFile(hWrite, cmd_buf, WriteLen, &nBytesWrite, NULL);
    assert(nBytesWrite == WriteLen);
    if (nb_sector && input && buffer != NULL) {
        /* Read data*/
        success = ReadFile(hRead, buffer, 512 * nb_sector, &nBytesRead, NULL);
        if (nBytesRead != 512 * nb_sector) {
            return -1;
        }
    }
    else if (nb_sector && buffer != NULL) {
        /* Write data*/
        success = WriteFile(hWrite, buffer, 512 * nb_sector, &nBytesRead, NULL);
        if (nBytesRead != 512 * nb_sector) {
            return -2;
        }
    }
    /* check command status */
    ReadLen = 32;
    success = ReadFile(hRead,
                       response_buf,
                       ReadLen,
                       &nBytesRead,
                       NULL);
    if (nBytesRead != ReadLen) {
        return -3;
    }
    success = VerifyCmdStatus(response_buf, tag);
    if (!success) {
        printf("Sector Read error.\n");
        return -4;
    }
    // close devices if needed
    if (hRead != INVALID_HANDLE_VALUE) {
        CloseHandle(hRead);
    }
    if (hWrite != INVALID_HANDLE_VALUE) {
        CloseHandle(hWrite);
    }
    return success;
}
/*byte based comamnd issuing, basicaly the start_sector and nb_sector in the command packets are set to zero,
   and meanwhile caller specify the byter number to transfer*/
int IssueCommand2(char cmd, int input, int nb_bytes, char* buffer) {
    char cmd_buf[64], response_buf[64];
    ULONG WriteLen, nBytesWrite, ReadLen, nBytesRead;
    int success = 1;
    HANDLE hRead = INVALID_HANDLE_VALUE, hWrite = INVALID_HANDLE_VALUE;
    int tag;
    srand( static_cast<unsigned int>( time(NULL) ) );
    tag = rand();
    hRead = OpenBulkUSB(0);     // bulk-in
    if (hRead == INVALID_HANDLE_VALUE) {
        assert(0);
        return -100;
    }
    hWrite = OpenBulkUSB(1);    // bulk-out
    if (hWrite == INVALID_HANDLE_VALUE) {
        assert(0);
        return -101;
    }
    /* Issue the command */
    FillCmd(cmd_buf, tag, cmd, 0, 0);  /*start_sector=0, 23x512=12K data to transfer*/
    WriteLen = CMD_LENGTH;
    WriteFile(hWrite, cmd_buf, WriteLen, &nBytesWrite, NULL);
    assert(nBytesWrite == WriteLen);
    if (nb_bytes && input && buffer != NULL) {
        /* Read data*/
        success = ReadFile(hRead, buffer, nb_bytes, &nBytesRead, NULL);
        if (nBytesRead != nb_bytes) {
            return -1;
        }
    }
    else if (nb_bytes && buffer != NULL) {
        /* Write data*/
        success = WriteFile(hWrite, buffer, nb_bytes, &nBytesRead, NULL);
        if (nBytesRead != nb_bytes) {
            return -2;
        }
    }
    /* check command status */
    ReadLen = 32;
    success = ReadFile(hRead,
                       response_buf,
                       ReadLen,
                       &nBytesRead,
                       NULL);
    if (nBytesRead != ReadLen) {
        return -3;
    }
    success = VerifyCmdStatus(response_buf, tag);
    if (!success) {
        printf("Sector Read error.\n");
        return -4;
    }
    // close devices if needed
    if (hRead != INVALID_HANDLE_VALUE) {
        CloseHandle(hRead);
    }
    if (hWrite != INVALID_HANDLE_VALUE) {
        CloseHandle(hWrite);
    }
    return success;
}
//
// ReadImageFromDockingSystem::
// option - 0 (upload image data only)
// 1 (rigi-flex board test followed by upload image data)
// 2 upload image data through fast GPIO link
//
#define SECTOR_PER_READ       32
void ReadImageFromDockingSystem(char* filename, int option) {
    char* pBuff;
    int i, total_sector;
    FILE* pFile;
    int success = 1, loop = 0;
    pFile = fopen(filename, "wb");
    assert(pFile);
    i = 0;
    total_sector = -1;  // init value
    pBuff = (char*)malloc(SECTOR_PER_READ * 512);
    // send image read command through Read function
    if (option == 2) {
        success = IssueCommand(CMD_UPLOAD_IMAGE2, 1, 0, 0, NULL);
        if (!success) {
            return;
        }
    }
    else if (option == 1) {
        success = IssueCommand(CMD_UPLOAD_RIGI_TEST, 1, 0, 0, NULL);
        if (!success) {
            return;
        }
    }
    else {
        success = IssueCommand(CMD_UPLOAD_IMAGE, 1, 0, 0, NULL);
        if (!success) {
            return;
        }
    }
    printf("Start image data upload ...\n");
    while (total_sector)
    {
        if (IssueCommand(CMD_UPLOADING, 1, IMAGE_SECTOR_START + i, SECTOR_PER_READ, pBuff) == TRUE) {
            if (total_sector == -1) {
                // this is the first time read.
                // if option = 0 or 1, read byte [0:3] to decide the total number of pages
                // if option = 2, read byte [4:7] to decide the total number of pages (product test images)
                if (option == 2) {
                    total_sector = SwapInt( * (int*)(pBuff + 4) );
                }
                else {
                    total_sector = SwapInt(* (int*)pBuff);
                }
                // If capsule power off before complete capture
                // the Extra Info will not be written (no total captured pages data)
                // It will send ESTIMATE_MAX_PAGE (defined in system.h) to docking system anyway...
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
        loop++;
        /*
           if(loop==30)
           {
            IssueCommand(CMD_UPLOAD_END, 0, 0, 0, 0);      //safely stop the picture uploading
            return ;
           }
         */
    }
    IssueCommand(CMD_UPLOAD_END, 0, 0, 0, 0);     // safely stop the picture uploading
    fclose(pFile);
    free(pBuff);
}
#if 0
int _cdecl main(
    int argc,
    char* argv[]) {
    ReadImageFromDockingSystem("image.bin", 0);
}
#endif
#if 0
int _cdecl main(
    int argc,
    char* argv[]) {
/*++
   Routine Description:

    Entry point to rwbulk.exe
    Parses cmdline, performs user-requested tests

   Arguments:

    argc, argv  standard console  'c' app arguments

   Return Value:

    Zero

   --*/
    char* poutBuf = NULL;
    UINT success = 1;
    FILE* pFile = NULL;
    // char fw_filename[] = "X:\\USBBULK\\bulkusb\\exe\\capsodrv\\capsodrv\\capsocam.bin";
    char fw_filename[] = "X:\\USBBULK\\bulkusb\\exe\\capsodrv\\capsodrv\\capsocore.bin";
    // char fw_filename[] = "X:\\USBBULK\\bulkusb\\exe\\capsodrv\\capsodrv\\capsoboot.bin";
    poutBuf = (char*)malloc(FIRMWARE_SIZE);
    assert(poutBuf);
    /*write firmware */
    printf("download %s\n", fw_filename);
    pFile = fopen(fw_filename, "rb");
    if (pFile == NULL) {
        printf("Open firmware file fails");
        return 0;
    }
    if (fread(poutBuf, 1, FIRMWARE_SIZE, pFile) != FIRMWARE_SIZE) {
        printf("Firmware read error");
    }
    /*output from PC, start_sector=0, 23x512=12K data to transfer*/
    success = IssueCommand(CMD_UPDATE_FW_CORE, 0, 0, FIRMWARE_SIZE / 512, poutBuf);
    if (success != 1) {
        printf("Firmware update error.\n");
    }
    if (poutBuf) {
        free(poutBuf);
        poutBuf = NULL;
    }
    return 0;
}
#endif
int _cdecl main(
    int argc,
    char* argv[]) {
    char serial_num[] = "CV090123-1234567";
    // char serial_num[]="ABCDEFGHIHKLMNSB";
    char buffer[64];
    IssueCommand2(CMD_READ_SERIAL_NUMBER, 1, 16, buffer);
    //
    strcpy(buffer, serial_num);
    IssueCommand2(CMD_WRITE_SERIAL_NUMBER, 0, 32, buffer);
    //
    IssueCommand2(CMD_READ_SERIAL_NUMBER, 1, 16, buffer);
}
#if 0
int _cdecl main(
    int argc,
    char* argv[]) {
/*++
   Routine Description:

    Entry point to rwbulk.exe
    Parses cmdline, performs user-requested tests

   Arguments:

    argc, argv  standard console  'c' app arguments

   Return Value:

    Zero

   --*/
    char* pinBuf = NULL, * poutBuf = NULL;
    unsigned long nBytesRead, nBytesWrite, nBytes;
    ULONG i, j;
    int ok;
    UINT success;
    HANDLE hRead = INVALID_HANDLE_VALUE, hWrite = INVALID_HANDLE_VALUE;
    char buf[1024];
    clock_t start, finish;
    ULONG totalBytes = 0L;
    double seconds;
    ULONG fail = 0L;
    FILE* pFile = NULL;
    int tag;
    // char fw_filename[] = "X:\\USBBULK\\bulkusb\\exe\\capsodrv\\capsodrv\\capsocam.bin";
    char fw_filename[] = "X:\\USBBULK\\bulkusb\\exe\\capsodrv\\capsodrv\\capsocore.bin";
    srand( time(NULL) );
    tag = rand();
    hRead = open_file(inPipe);
    assert(hRead);
    hWrite = open_file(outPipe);
    pinBuf = (char*)malloc(FIRMWARE_SIZE);
    poutBuf = (char*)malloc(FIRMWARE_SIZE);
    /* write command */
    FillCmd(poutBuf, tag, CMD_UPDATE_FW_CORE, 0, FIRMWARE_SIZE / 512);  /*start_sector=0, 23x512=12K data to transfer*/
    WriteLen = CMD_LENGTH;
    WriteFile(hWrite, poutBuf, WriteLen, &nBytesWrite, NULL);
    assert(nBytesWrite == WriteLen);
    /*write firmware */
    printf("download %s\n", fw_filename);
    pFile = fopen(fw_filename, "rb");
    if (pFile == NULL) {
        printf("Open firmware file fails");
        return 0;
    }
    if (fread(poutBuf, 1, FIRMWARE_SIZE, pFile) != FIRMWARE_SIZE) {
        printf("Firmware read error");
    }
    WriteLen =  FIRMWARE_SIZE;
    WriteFile(hWrite, poutBuf, WriteLen, &nBytesWrite, NULL);
    assert(nBytesWrite == WriteLen);
    /* check status */
    ReadLen = 32;
    success = ReadFile(hRead,
                       pinBuf,
                       ReadLen,
                       &nBytesRead,
                       NULL);
    assert(nBytesRead == ReadLen);
    success = VerifyCmdStatus(pinBuf, tag);
    if (!success) {
        printf("Firmware update error.\n");
    }
    if (pinBuf) {
        free(pinBuf);
        pinBuf = NULL;
    }
    if (poutBuf) {
        free(poutBuf);
        poutBuf = NULL;
    }
    // close devices if needed
    if (hRead != INVALID_HANDLE_VALUE) {
        CloseHandle(hRead);
    }
    if (hWrite != INVALID_HANDLE_VALUE) {
        CloseHandle(hWrite);
    }
    return 0;
}
#endif
/*
   int _tmain(int argc, _TCHAR* argv[])
   {
    return 0;
   }
 */
