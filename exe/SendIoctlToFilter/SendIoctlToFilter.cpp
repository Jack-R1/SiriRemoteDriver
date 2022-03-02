/*++

Based of Windows Driver Samples - Toaster Project
https://github.com/microsoft/Windows-driver-samples/tree/master/general/toaster

Abstract:

	Open control device and send Ioctl requests.

Environment:

	usermode console application

--*/

#include <basetyps.h>
#include <stdlib.h>
#include <wtypes.h>
#include <setupapi.h>
#include <initguid.h>
#include <stdio.h>
#include <string.h>
#include <winioctl.h>
#include <conio.h>
#include <dontuse.h>

#define IOCTL_FIX_HCI_L2CAP_HEADERS_ON CTL_CODE(FILE_DEVICE_UNKNOWN, 0x11, METHOD_BUFFERED, FILE_READ_DATA)
#define IOCTL_FIX_HCI_L2CAP_HEADERS_OFF CTL_CODE(FILE_DEVICE_UNKNOWN,0x10, METHOD_BUFFERED, FILE_READ_DATA)
#define IOCTL_DEBUG_DATA_IN_ON CTL_CODE(FILE_DEVICE_UNKNOWN, 0x21, METHOD_BUFFERED, FILE_READ_DATA)
#define IOCTL_DEBUG_DATA_IN_OFF CTL_CODE(FILE_DEVICE_UNKNOWN, 0x20, METHOD_BUFFERED, FILE_READ_DATA)
#define IOCTL_DEBUG_DATA_OUT_ON CTL_CODE(FILE_DEVICE_UNKNOWN, 0x31, METHOD_BUFFERED, FILE_READ_DATA)
#define IOCTL_DEBUG_DATA_OUT_OFF CTL_CODE(FILE_DEVICE_UNKNOWN, 0x30, METHOD_BUFFERED, FILE_READ_DATA)

BOOL bFixHciL2cap = FALSE;
BOOL bDebugDataIn = FALSE;
BOOL bDebugDataOut = FALSE;

HANDLE hControlDevice;

VOID
Usage()
{
	printf("Usage:\n");
	printf("-f to apply HCI/L2CAP headers fix for BLE 4.0\n");
	printf("-i to DbgPrint() incoming data to a kernel debug log viewer like Sysinternals DebugView\n");
	printf("-o to DbgPrint() outgoing data to a kernel debug log viewer like Sysinternals DebugView\n");
	return;
}


int SendIoctlToFilterDevice(DWORD ioctlControlRequest, const char * ioctlControlRequestName)
{
	ULONG	bytes;
	DWORD	lastError;

	if (!DeviceIoControl(hControlDevice,
		ioctlControlRequest,
		NULL, 0,
		NULL, 0,
		&bytes, NULL)) {

		lastError = GetLastError();
		printf("Ioctl to SiriRemoteFilter device failed\n");
		printf("%s request failed:0x%x\n", ioctlControlRequestName, lastError);
		CloseHandle(hControlDevice);
		return 0;
	}

	printf("Ioctl %s to SiriRemoteFilter device succeeded\n", ioctlControlRequestName);

	return 1;
}

INT __cdecl
main(
	_In_ int argc,
	_In_reads_(argc) PCHAR argv[]
)
{
	int		ch;
	DWORD	lastError;
	int		retValue = 0;

	for (int i = 0; i < argc; i++) {
		if (argv[i][0] == '-' ||
			argv[i][0] == '/') {
			switch (argv[i][1]) {
			case 'f':
			case 'F':
				bFixHciL2cap = TRUE;
				break;
			case 'i':
			case 'I':
				bDebugDataIn = TRUE;
				break;
			case 'o':
			case 'O':
				bDebugDataOut = TRUE;
				break;
			default:
				Usage();
				return retValue;
			}
		}
	}

	printf("\nOpening handle to the control device: %s\n", "\\\\.\\SiriRemoteFilter");

	//
	// Open handle to the control device. Please note that even
	// a non-admin user can open handle to the device with
	// FILE_READ_ATTRIBUTES | SYNCHRONIZE DesiredAccess and send IOCTLs if the
	// IOCTL is defined with FILE_ANY_ACCESS. So for better security avoid
	// specifying FILE_ANY_ACCESS in your IOCTL defintions.
	// If the IOCTL is defined to have FILE_READ_DATA access rights, you can
	// open the device with GENERIC_READ and call DeviceIoControl.
	// If the IOCTL is defined to have FILE_WRITE_DATA access rights, you can
	// open the device with GENERIC_WRITE and call DeviceIoControl.
	//
	hControlDevice = CreateFile(TEXT("\\\\.\\SiriRemoteFilter"),
		GENERIC_READ, // Only read access
		0, // FILE_SHARE_READ | FILE_SHARE_WRITE
		NULL, // no SECURITY_ATTRIBUTES structure
		OPEN_EXISTING, // No special create flags
		0, // No special attributes
		NULL); // No template file

	if (INVALID_HANDLE_VALUE == hControlDevice) {
		lastError = GetLastError();
		printf("Failed to open SiriRemoteFilter device\n");
		printf("Error in CreateFile: %x\n", lastError);
		retValue = 1;
		goto exit;
	}

	if (bFixHciL2cap)
	{
		if (!SendIoctlToFilterDevice(IOCTL_FIX_HCI_L2CAP_HEADERS_ON, "IOCTL_FIX_HCI_L2CAP_HEADERS_ON"))
		{
			retValue = 1;
			goto exit;
		}
	}
	else
	{
		if (!SendIoctlToFilterDevice(IOCTL_FIX_HCI_L2CAP_HEADERS_OFF, "IOCTL_FIX_HCI_L2CAP_HEADERS_OFF"))
		{
			retValue = 1;
			goto exit;
		}
	}

	if (bDebugDataIn)
	{
		if (!SendIoctlToFilterDevice(IOCTL_DEBUG_DATA_IN_ON, "IOCTL_DEBUG_DATA_IN_ON"))
		{
			retValue = 1;
			goto exit;
		}
	}
	else
	{
		if (!SendIoctlToFilterDevice(IOCTL_DEBUG_DATA_IN_OFF, "IOCTL_DEBUG_DATA_IN_OFF"))
		{
			retValue = 1;
			goto exit;
		}
	}

	if (bDebugDataOut)
	{
		if (!SendIoctlToFilterDevice(IOCTL_DEBUG_DATA_OUT_ON, "IOCTL_DEBUG_DATA_OUT_ON"))
		{
			retValue = 1;
			goto exit;
		}
	}
	else
	{
		if (!SendIoctlToFilterDevice(IOCTL_DEBUG_DATA_OUT_OFF, "IOCTL_DEBUG_DATA_OUT_OFF"))
		{
			retValue = 1;
			goto exit;
		}
	}

	printf("\nPress any key to exit...\n");
	fflush(stdin);
	ch = _getche();

exit:
	CloseHandle(hControlDevice);
	return retValue;
}