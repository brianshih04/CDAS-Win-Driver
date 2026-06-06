#ifndef __BULKUSB_API__H_
#define __BULKUSB_API__H_
// #include "stdafx.h"
extern "C" {
#define CDAS1_VID_PID _T("USB\\VID_03EB&PID_941C")
#define CDAS2_VID_PID _T("USB\\VID_0638&PID_0931")
#define CDAS_PRODUCTION_TEST_VID_PID _T("USB\\VID_03EB&PID_952C")
//#define CDAS2_VID_PID _T("USB\\VID_0638&PID_0931&REV_0100")

/*
   1. Open HANDLE to read/write
   2. Can also be used to test if the device is connected or not
 */
HANDLE OpenBulkUSB(int bOutput);

/*
   0 - docking system
   1 - production test system
   By default it is zero
 */
void ChooseUSBDevice(int);

/*
   Compatibility no-op in the WinUSB sample.
 */
void ChooseUSBMode(int);

// get the number of CDAS devices
int GetUSBDeviceNum(bool* foundCDAS1, bool* foundCDAS2);

// check if a single CDAS1 is attached
bool IsSingleCDAS1Attached();

// check if a single CDAS2 is attached
bool IsSingleCDAS2Attached();

// 0 for any CDAS
// 1 for CDAS1
// 2 for CDAS2
void SelectCDASVersion(int version);
}
#endif
