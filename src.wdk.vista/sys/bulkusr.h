/*++

   Copyright (c) 2000  Microsoft Corporation

   Module Name:

    sSUsr.h

   Abstract:

   Environment:

    Kernel mode

   Notes:

    Copyright (c) 2000 Microsoft Corporation.
    All Rights Reserved.

   --*/
#ifndef _BULKUSB_USER_H
#define _BULKUSB_USER_H
#include <initguid.h>
// ////By Kernel////////////////////////////////////////////////////////////
// {6068EB61-98E7-4c98-9E20-1F068295909A}
DEFINE_GUID(GUID_CLASS_I82930_BULK,
// 0xc20e4f22, 0x44b8, 0x4feb,   0x8f, 0x31, 0xf7, 0x8e, 0x29, 0xcc, 0xd0, 0x35);// production test system
            0x873fdf, 0x62a8, 0x11d1, 0xaa, 0x5e, 0x0, 0xc0, 0x4f, 0xb1, 0x72, 0x8b); // docking system
// /////By User Mode/////////////////////////////////////////////////////////
DEFINE_GUID(GUID_CLASS_DOCKING_SYSTEM,
            0x873fdf, 0x62a8, 0x11d1, 0xaa, 0x5e, 0x0, 0xc0, 0x4f, 0xb1, 0x72, 0x8b);
DEFINE_GUID(GUID_CLASS_PRODUCTION_TEST,
            0xc20e4f22, 0x44b8, 0x4feb, 0x8f, 0x31, 0xf7, 0x8e, 0x29, 0xcc, 0xd0, 0x35); // production test system
// /////////////////////////////////////////////////////////////////////////
#define BULKUSB_IOCTL_INDEX             0x0000
#define IOCTL_BULKUSB_GET_CONFIG_DESCRIPTOR CTL_CODE(FILE_DEVICE_UNKNOWN,     \
                                                     BULKUSB_IOCTL_INDEX,     \
                                                     METHOD_BUFFERED,         \
                                                     FILE_ANY_ACCESS)
#define IOCTL_BULKUSB_RESET_DEVICE          CTL_CODE(FILE_DEVICE_UNKNOWN,     \
                                                     BULKUSB_IOCTL_INDEX + 1, \
                                                     METHOD_BUFFERED,         \
                                                     FILE_ANY_ACCESS)
#define IOCTL_BULKUSB_RESET_PIPE            CTL_CODE(FILE_DEVICE_UNKNOWN,     \
                                                     BULKUSB_IOCTL_INDEX + 2, \
                                                     METHOD_BUFFERED,         \
                                                     FILE_ANY_ACCESS)

#ifdef CDAS2_CHANGES
#define FILE_DEVICE_CDAS1            0x00000071
#define FILE_DEVICE_CDAS2            0x00000072
#define IOCTL_BULKUSB_GET_CONFIG_DESCRIPTOR_CDAS1   CTL_CODE(FILE_DEVICE_CDAS1,     \
                                                     BULKUSB_IOCTL_INDEX,     \
                                                     METHOD_BUFFERED,         \
                                                     FILE_ANY_ACCESS)
#define IOCTL_BULKUSB_GET_CONFIG_DESCRIPTOR_CDAS2   CTL_CODE(FILE_DEVICE_CDAS2,     \
                                                     BULKUSB_IOCTL_INDEX,     \
                                                     METHOD_BUFFERED,         \
                                                     FILE_ANY_ACCESS)
#define IOCTL_BULKUSB_RESET_DEVICE_CDAS1    CTL_CODE(FILE_DEVICE_CDAS1,     \
                                                     BULKUSB_IOCTL_INDEX + 1, \
                                                     METHOD_BUFFERED,         \
                                                     FILE_ANY_ACCESS)
#define IOCTL_BULKUSB_RESET_DEVICE_CDAS2    CTL_CODE(FILE_DEVICE_CDAS2,     \
                                                     BULKUSB_IOCTL_INDEX + 1, \
                                                     METHOD_BUFFERED,         \
                                                     FILE_ANY_ACCESS)
#define IOCTL_BULKUSB_RESET_PIPE_CDAS1      CTL_CODE(FILE_DEVICE_CDAS1,     \
                                                     BULKUSB_IOCTL_INDEX + 2, \
                                                     METHOD_BUFFERED,         \
                                                     FILE_ANY_ACCESS)
#define IOCTL_BULKUSB_RESET_PIPE_CDAS2      CTL_CODE(FILE_DEVICE_CDAS2,     \
                                                     BULKUSB_IOCTL_INDEX + 2, \
                                                     METHOD_BUFFERED,         \
                                                     FILE_ANY_ACCESS)
#endif

#endif
