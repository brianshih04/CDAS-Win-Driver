#include "stdafx.h"
#include <setupapi.h>
#include <basetyps.h>
#include "windef.h"
#include "wingdi.h"
#include "winddi.h"
#include "devioctl.h"
#include "usbdi.h"
#include "..\Sys\BulkUsr.h"
#include "..\Include\BulkUSB_API.h"
volatile static int device_type = 0;  // 0 is docking system
volatile static int cdas_version = 0;	// 0 for any CDAS, 1 for CDAS1, 2 for CDAS2
#define MyAssert(x)
// #define NOISY(_x_) printf _x_ ;
#define NOISY(_x_)
static wchar_t inPipe[32] = TEXT("PIPE00");     // pipe name for bulk input pipe on our test board
static wchar_t outPipe[32] = TEXT("PIPE01");    // pipe name for bulk output pipe on our test board
static wchar_t completeDeviceName[256] = TEXT("");  // generated from the GUID registered by the driver itself
static BOOL fDumpUsbConfig = FALSE;    // flags set in response to console command line switches
static BOOL fDumpReadData = FALSE;
static BOOL fRead = FALSE;
static BOOL fWrite = FALSE;
static int gDebugLevel = 1;      // higher == more verbose, default is 1, 0 turns off all
static ULONG IterationCount = 1; // count of iterations of the test we are to perform
static int WriteLen = 0;         // #bytes to write
static int ReadLen = 0;          // #bytes to read
static int asynchronous_mode = 1;
#define NUMBEROFDEVICES	4
#define DEVICEDESCRIPTORLENGTH	1024

// check for CDAS1-CDAS2 is hard-coded in GetUSBDeviceNum() and OpenUsbDevice()

// functions

/*++
   Routine Description:
    Given the HardwareDeviceInfo, representing a handle to the plug and
    play information, and deviceInfoData, representing a specific usb device,
    open that device and fill in all the relevant information in the given
    USB_DEVICE_DESCRIPTOR structure.
   Arguments:
    HardwareDeviceInfo:  handle to info obtained from Pnp mgr via SetupDiGetClassDevs()
    DeviceInfoData:      ptr to info obtained via SetupDiEnumDeviceInterfaces()
   Return Value:
    return HANDLE if the open and initialization was successfull,
        else INVLAID_HANDLE_VALUE.
   --*/
HANDLE
OpenOneDevice(
    IN HDEVINFO HardwareDeviceInfo,
    IN PSP_DEVICE_INTERFACE_DATA DeviceInfoData,
    IN wchar_t* devName
    ) {
    PSP_DEVICE_INTERFACE_DETAIL_DATA functionClassDeviceData = NULL;
    ULONG predictedLength = 0;
    ULONG requiredLength = 0;
    HANDLE hOut = INVALID_HANDLE_VALUE;
    
    // allocate a function class device data structure to receive the
    // goods about this particular device.
    SetupDiGetDeviceInterfaceDetail(
        HardwareDeviceInfo,
        DeviceInfoData,
        NULL,     // probing so no output buffer yet
        0,     // probing so output buffer length of zero
        &requiredLength,
        NULL);     // not interested in the specific dev-node
    predictedLength = requiredLength;
    // sizeof (SP_FNCLASS_DEVICE_DATA) + 512;
    functionClassDeviceData = (PSP_DEVICE_INTERFACE_DETAIL_DATA) malloc(predictedLength);
    if (NULL == functionClassDeviceData) {
        return INVALID_HANDLE_VALUE;
    }
    functionClassDeviceData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
    
    // Retrieve the information from Plug and Play.
    if ( !SetupDiGetDeviceInterfaceDetail(
            HardwareDeviceInfo,
            DeviceInfoData,
            functionClassDeviceData,
            predictedLength,
            &requiredLength,
            NULL) ) {
        free(functionClassDeviceData);
        return INVALID_HANDLE_VALUE;
    }
    wcscpy(devName, functionClassDeviceData->DevicePath);
    NOISY( ("Attempting to open %s\n", devName) );
    hOut = CreateFile(
        functionClassDeviceData->DevicePath,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,           // no SECURITY_ATTRIBUTES structure
        OPEN_EXISTING,           // No special create flags
        0,           // No special attributes
        NULL);           // No template file
    if (INVALID_HANDLE_VALUE == hOut) {
        //printf("FAILED to open %s\n", devName);
    }
    free(functionClassDeviceData);
    return hOut;
}

/*++
   Routine Description:
	Do the required PnP things in order to find
	the next available proper device in the system at this time.
   Arguments:
    pGuid:      ptr to GUID registered by the driver itself
	cdasVersion: CDAS version number: 0 for any version, 1 for CDAS1, 2 for CDAS2
    outNameBuf: the generated name for this device
   Return Value:
    return HANDLE if the open and initialization was successful,
        else INVLAID_HANDLE_VALUE.
   This function is ONLY called in GetUsbDeviceFileName()
   --*/
HANDLE
OpenUsbDevice(LPGUID pGuid, wchar_t* outNameBuf) {
    HANDLE hOut = INVALID_HANDLE_VALUE;
    
    // Open a handle to the plug and play dev node.
    // SetupDiGetClassDevs() returns a device information set that contains info on all
    // installed devices of a specified class.
    HDEVINFO hardwareDeviceInfo = SetupDiGetClassDevs(
        pGuid,
        NULL,                        // Define no enumerator (global)
        NULL,                        // Define no
        (DIGCF_PRESENT |             // Only Devices present
         DIGCF_DEVICEINTERFACE) );   // Function class devices.

	if (hardwareDeviceInfo == INVALID_HANDLE_VALUE) {
        return INVALID_HANDLE_VALUE;
    }

	SP_DEVICE_INTERFACE_DATA deviceInterfaceData;
	deviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);	
    
    // Take a wild guess at the number of devices we have;
    // Be prepared to realloc and retry if there are more than we guessed
	ULONG NumberDevices = NUMBEROFDEVICES; 
    ULONG i = 0;
	BOOLEAN done = FALSE;
    while (!done)
    {
        for (; i < NumberDevices; i++) {
            // SetupDiEnumDeviceInterfaces() returns information about device interfaces
            // exposed by one or more devices. Each call returns information about one interface;
            // the routine can be called repeatedly to get information about several interfaces
            // exposed by one or more devices.
            if ( SetupDiEnumDeviceInterfaces(hardwareDeviceInfo,
                                             0, // We don't care about specific PDOs
                                             pGuid,
                                             i,
                                             &deviceInterfaceData) ) {
				if (cdas_version == 0) {	// search for any CDAS
					hOut = OpenOneDevice(hardwareDeviceInfo, &deviceInterfaceData, outNameBuf);
					if (hOut != INVALID_HANDLE_VALUE) {
						done = TRUE;
						break;
					}
				}
				else {
					// identify if CDAS1 or CDAS2 using VID and PID
					SP_DEVINFO_DATA deviceInfoData;
					deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);	// have to set this before calling SetupDiEnumDeviceInfo()
					DWORD dwPropertyRegDataType;
					TCHAR szDesc[DEVICEDESCRIPTORLENGTH];
					DWORD dwSize;
					if (SetupDiEnumDeviceInfo(hardwareDeviceInfo, i, &deviceInfoData)) {					
						bool found = false;
						// retrieve the device hardware ID
						SetupDiGetDeviceRegistryProperty(hardwareDeviceInfo, 
														&deviceInfoData, 
														SPDRP_HARDWAREID, 
														&dwPropertyRegDataType, 
														(BYTE*)szDesc, 
														sizeof(szDesc), 
														&dwSize);						
						if (cdas_version == 1) {	// search for CDAS1 only
							if (_tcsstr(szDesc, CDAS1_VID_PID) != NULL) {
								found = true;
							}
						}
						else if (cdas_version == 2) {	// search for CDAS2 only
							if (_tcsstr(szDesc, CDAS2_VID_PID) != NULL) {
								found = true;
							}
						}
						if (found) {
							hOut = OpenOneDevice(hardwareDeviceInfo, &deviceInterfaceData, outNameBuf);
							if (hOut != INVALID_HANDLE_VALUE) {
								done = TRUE;
								break;
							}
						}
					}
				}              
            }
            else {
                if ( ERROR_NO_MORE_ITEMS == GetLastError() ) {
                    done = TRUE;
                    break;
                }
            }
        }
    }
    // SetupDiDestroyDeviceInfoList() destroys a device information set
    // and frees all associated memory.
    SetupDiDestroyDeviceInfoList(hardwareDeviceInfo);
    return hOut;
}

/*++
   Routine Description:
    Given a ptr to a driver-registered GUID, give us a string with the device name
    that can be used in a CreateFile() call.
    Actually briefly opens and closes the device and sets outBuf if successfull;
    returns FALSE if not
   Arguments:
    pGuid:      ptr to GUID registered by the driver itself
	cdasVersion: CDAS version number: 0 for any version, 1 for CDAS1, 2 for CDAS2
    outNameBuf: the generated zero-terminated name for this device
   Return Value:
    TRUE on success else FALSE
   This function is ONLY called in open_file()
   --*/
BOOL
GetUsbDeviceFileName(LPGUID pGuid, wchar_t* outNameBuf) {
    HANDLE hDev = OpenUsbDevice(pGuid, outNameBuf);
    if (hDev != INVALID_HANDLE_VALUE) {
        CloseHandle(hDev);
        return TRUE;
    }
    return FALSE;
}

/*++
   Routine Description:
    Called by main() to open an instance of our device after obtaining its name
   Arguments:
    None
   Return Value:
    Device handle on success else NULL
   --*/
HANDLE
open_file(wchar_t* filename) {
    int success = 1;
    HANDLE h;
    LPGUID lpguid;

    if (device_type == 0) {
        lpguid = (LPGUID)&GUID_CLASS_DOCKING_SYSTEM;
    }
    else {
        lpguid = (LPGUID)&GUID_CLASS_PRODUCTION_TEST;
    }

    if ( !GetUsbDeviceFileName(lpguid, completeDeviceName) ) {
        NOISY( ( "Failed to GetUsbDeviceFileName err - %d\n", GetLastError() ) );
        return INVALID_HANDLE_VALUE;
    }

    wcscat_s(completeDeviceName,
             TEXT("\\")
             );
    if ( ( wcslen(completeDeviceName) + wcslen(filename) ) > 255 ) {
        NOISY( ("Failed to open handle - possibly long filename\n") );
        return INVALID_HANDLE_VALUE;
    }
    wcscat_s(completeDeviceName,
             filename
             );
    NOISY( ("completeDeviceName = (%s)\n", completeDeviceName) );
    if (asynchronous_mode) {
        h = CreateFile(completeDeviceName,
                       GENERIC_WRITE | GENERIC_READ,
                       FILE_SHARE_WRITE | FILE_SHARE_READ,
                       NULL,
                       OPEN_EXISTING,
                       FILE_FLAG_OVERLAPPED,
                       NULL);
    }
    else {
        h = CreateFile(completeDeviceName,
                       GENERIC_WRITE | GENERIC_READ,
                       FILE_SHARE_WRITE | FILE_SHARE_READ,
                       NULL,
                       OPEN_EXISTING,
                       0,
                       NULL);
    }
    if (h == INVALID_HANDLE_VALUE) {
        NOISY( ( "Failed to open (%s) = %d", completeDeviceName, GetLastError() ) );
        success = 0;
    }
    else {
        NOISY( ("Opened successfully.\n") );
    }
    return h;
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
    char* poutBuf = NULL;
    UINT success = 1;
    FILE* pFile = NULL;
    // char fw_filename[] = "X:\\USBBULK\\bulkusb\\exe\\capsodrv\\capsodrv\\capsocam.bin";
    char fw_filename[] = "X:\\USBBULK\\bulkusb\\exe\\capsodrv\\capsodrv\\capsocore.bin";
    // char fw_filename[] = "X:\\USBBULK\\bulkusb\\exe\\capsodrv\\capsodrv\\capsoboot.bin";
    poutBuf = (char*)malloc(FIRMWARE_SIZE);
    MyAssert(poutBuf);
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
    MyAssert(hRead);
    hWrite = open_file(outPipe);
    pinBuf = (char*)malloc(FIRMWARE_SIZE);
    poutBuf = (char*)malloc(FIRMWARE_SIZE);
    /* write command */
    FillCmd(poutBuf, tag, CMD_UPDATE_FW_CORE, 0, FIRMWARE_SIZE / 512);  /*start_sector=0, 23x512=12K data to transfer*/
    WriteLen = CMD_LENGTH;
    WriteFile(hWrite, poutBuf, WriteLen, &nBytesWrite, NULL);
    MyAssert(nBytesWrite == WriteLen);
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
    MyAssert(nBytesWrite == WriteLen);
    /* check status */
    ReadLen = 32;
    success = ReadFile(hRead,
                       pinBuf,
                       ReadLen,
                       &nBytesRead,
                       NULL);
    MyAssert(nBytesRead == ReadLen);
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

int GetUSBDeviceNum(bool* foundCDAS1, bool* foundCDAS2) {
	int nummDeviceFound = 0;
	*foundCDAS1 = false;
	*foundCDAS2 = false;

	// determine which GUID to use
	LPGUID lpguid;
    if (device_type == 0) {
        lpguid = (LPGUID)&GUID_CLASS_DOCKING_SYSTEM;
    }
    else {
        lpguid = (LPGUID)&GUID_CLASS_PRODUCTION_TEST;
    }
  
    // Get "active" device information set using GUID.
	HDEVINFO hardwareDeviceInfo = SetupDiGetClassDevs(lpguid, NULL, NULL, (DIGCF_PRESENT | DIGCF_DEVICEINTERFACE) ); 

	// iterate through the found devices		
	SP_DEVICE_INTERFACE_DATA deviceInterfaceData;
	deviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA); 
	int i = 0;
    while ( SetupDiEnumDeviceInterfaces(hardwareDeviceInfo, 0, lpguid, i, &deviceInterfaceData) ) {
		SP_DEVINFO_DATA deviceInfoData;
		deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);	// have to set this before calling SetupDiEnumDeviceInfo()
		DWORD dwPropertyRegDataType;
		TCHAR szDesc[1024];
		DWORD dwSize;
		if (SetupDiEnumDeviceInfo(hardwareDeviceInfo, i, &deviceInfoData))
		{
			// get device's hardware ID which should contain the VID and PID
			if (SetupDiGetDeviceRegistryProperty(hardwareDeviceInfo, &deviceInfoData, SPDRP_HARDWAREID, &dwPropertyRegDataType, (BYTE*)szDesc, sizeof(szDesc), &dwSize))
			{			
				if (_tcsstr(szDesc, CDAS1_VID_PID) != NULL)
				{
					*foundCDAS1 = true;
					nummDeviceFound++;
				}
				else if (_tcsstr(szDesc, CDAS2_VID_PID) != NULL)
				{
					*foundCDAS2 = true;
					nummDeviceFound++;
				}					
			}
		}
		i++;
	}
	return nummDeviceFound;
}


bool IsSingleCDAS1Attached()
{
	bool foundCDAS1, foundCDAS2;
	int numAttachedDevice = GetUSBDeviceNum(&foundCDAS1, &foundCDAS2);
	if (numAttachedDevice != 1)
	{
		return false;
	}
	return foundCDAS1;
}


bool IsSingleCDAS2Attached()
{
	bool foundCDAS1, foundCDAS2;
	int numAttachedDevice = GetUSBDeviceNum(&foundCDAS1, &foundCDAS2);
	if (numAttachedDevice != 1)
	{
		return false;
	}
	return foundCDAS2;
}

// this function is used by the "client" to open a USB connection to the docking station
// output = 0 for read from docking station to PC
// output = 1 for write from PC to docking station
HANDLE OpenBulkUSB(int output) {
	bool foundCDAS1, foundCDAS2;
	int numAttachedDevice = GetUSBDeviceNum(&foundCDAS1, &foundCDAS2);
	if (numAttachedDevice > 0) {
		if (foundCDAS2) {
			SelectCDASVersion(2);
		}
		else {
			SelectCDASVersion(1);
		}
	}
    if (output == 0) {
        return open_file(inPipe);
    }
    else {
        return open_file(outPipe);
    }
}


void ChooseUSBDevice(int type) {
    device_type = type;
}


void ChooseUSBMode(int mode) {
    asynchronous_mode = mode;
}


// version = 0 for any CDAS
//           1 for CDAS1
//           2 for CDAS2
void SelectCDASVersion(int version) {
	cdas_version = version;
}


int GetCDASVersion()
{
	return cdas_version;
}
