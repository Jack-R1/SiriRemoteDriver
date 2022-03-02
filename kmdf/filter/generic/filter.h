/*++

Based of Windows Driver Samples - Toaster Project
https://github.com/microsoft/Windows-driver-samples/tree/master/general/toaster

Module Name:

    filter.h

Abstract:

    Contains structure definitions and function prototypes for sideband filter driver.

Environment:

    Kernel mode

--*/

#include <ntddk.h>
#include <wdf.h>
#include <wdmsec.h> // for SDDLs
#define NTSTRSAFE_LIB
#include <ntstrsafe.h>

#if !defined(_FILTER_H_)
#define _FILTER_H_


#define DRIVERNAME "Generic.sys: "

//
// Change the following define to 1 if you want to forward
// the request with a completion routine.
//
#define FORWARD_REQUEST_WITH_COMPLETION 1


typedef struct _FILTER_EXTENSION
{
    WDFDEVICE WdfDevice;
    // More context data here

}FILTER_EXTENSION, *PFILTER_EXTENSION;


WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(FILTER_EXTENSION,
                                        FilterGetData)

#define NTDEVICE_NAME_STRING      L"\\Device\\SiriRemoteFilter"
#define SYMBOLIC_NAME_STRING      L"\\DosDevices\\SiriRemoteFilter"

typedef struct _CONTROL_DEVICE_EXTENSION {

    PVOID   ControlData; // Store your control data here

} CONTROL_DEVICE_EXTENSION, *PCONTROL_DEVICE_EXTENSION;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(CONTROL_DEVICE_EXTENSION,
                                             ControlGetData)
DRIVER_INITIALIZE DriverEntry;

EVT_WDF_DRIVER_DEVICE_ADD FilterEvtDeviceAdd;
EVT_WDF_DRIVER_UNLOAD FilterEvtDriverUnload;
EVT_WDF_DEVICE_CONTEXT_CLEANUP FilterEvtDeviceContextCleanup;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL FilterEvtIoDeviceControl;
EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL FilterEvtIoInternalDeviceControl;

NTSTATUS
FilterCreateControlDevice(
    _In_ WDFDEVICE Device
    );

VOID
FilterDeleteControlDevice(
    _In_ WDFDEVICE Device
    );
    
VOID
FilterForwardRequest(
    IN WDFREQUEST Request,
    IN WDFIOTARGET Target
    );

#if FORWARD_REQUEST_WITH_COMPLETION

VOID
FilterForwardRequestWithCompletionRoutine(
    IN WDFREQUEST Request,
    IN WDFIOTARGET Target
    );

VOID
FilterRequestCompletionRoutine(
    IN WDFREQUEST                  Request,
    IN WDFIOTARGET                 Target,
    PWDF_REQUEST_COMPLETION_PARAMS CompletionParams,
    IN WDFCONTEXT                  Context
   );

#endif //FORWARD_REQUEST_WITH_COMPLETION

#endif

