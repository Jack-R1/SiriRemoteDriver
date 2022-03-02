/*++

Based of Windows Driver Samples - Toaster Project
https://github.com/microsoft/Windows-driver-samples/tree/master/general/toaster

Module Name:

    filter.c

Abstract:

    This module shows how to a write a generic filter driver. The driver demonstrates how 
    to support device I/O control requests through queues. All the I/O requests passed on to 
    the lower driver. This filter driver shows how to handle IRP postprocessing by forwarding 
    the requests with and without a completion routine. To forward with a completion routine
    set the define FORWARD_REQUEST_WITH_COMPLETION to 1. 

Environment:

    Kernel mode

--*/

#include "filter.h"

/*
 Typically only user mode applications (usbui) or the hub driver include this
   file, USB drivers should use usbdrivr.h usb bus drivers should include
   usbkern.h
*/
//#include "usbioctl.h"
#include "usbdrivr.h"

//Global bool variables used by our filter driver and set from the userland 
//application. 
//FIX_HCI_L2CAP_HEADERS is set to apply a fix to usb packet headers when 
//the system has designated that the bluetooth adapter le version can't 
//handle more than 23 att bytes of data. (This turns out to not be the 
//case and the raw data is full length from BTHUSB lower module)
BOOLEAN FIX_HCI_L2CAP_HEADERS = FALSE;
BOOLEAN DEBUG_DATA_IN = FALSE;
BOOLEAN DEBUG_DATA_OUT = FALSE;

//Code for Dump copied from the internet, cant recall who to credit???
void Dump(int Direction, unsigned char * Bfr, size_t Count)
{
	for (; Count >= 16; Count -= 16, Bfr += 16)
	{
		if (Direction == USBD_TRANSFER_DIRECTION_IN && DEBUG_DATA_IN)
			DbgPrint("DATA IN: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
				Bfr[0], Bfr[1], Bfr[2], Bfr[3], Bfr[4], Bfr[5], Bfr[6], Bfr[7],
				Bfr[8], Bfr[9], Bfr[10], Bfr[11], Bfr[12], Bfr[13], Bfr[14],
				Bfr[15]
				);
		else if (Direction == USBD_TRANSFER_DIRECTION_OUT && DEBUG_DATA_OUT)
			DbgPrint("DATA OUT: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
				Bfr[0], Bfr[1], Bfr[2], Bfr[3], Bfr[4], Bfr[5], Bfr[6], Bfr[7],
				Bfr[8], Bfr[9], Bfr[10], Bfr[11], Bfr[12], Bfr[13], Bfr[14],
				Bfr[15]
				);
	}

	if (Count)
	{
		char sz[80];
		for (int i = 0; i < Count; i++)
			sprintf(sz + i * 3, " %02x", Bfr[i]);

		if (Direction == USBD_TRANSFER_DIRECTION_IN && DEBUG_DATA_IN)
			DbgPrint("DATA IN:%s\n", sz);
		else if (Direction == USBD_TRANSFER_DIRECTION_OUT && DEBUG_DATA_OUT)
			DbgPrint("DATA OUT:%s\n", sz);
	}
}

void DumpSingleLine(int Direction, unsigned char * Bfr, size_t Count)
{
	char sz[450];

	if (Count <= 140) //140 * 3 = 420
	{
		for (int i = 0; i < Count; i++)
			sprintf(sz + i * 3, " %02x", Bfr[i]);
	}
	else
	{
		for (int i = 0; i < 140; i++)
			sprintf(sz + i * 3, " %02x", Bfr[i]);

		sprintf(sz + 140 * 3, " ...");
	}

	if (Direction == USBD_TRANSFER_DIRECTION_IN && DEBUG_DATA_IN)
		DbgPrint("DATA IN:%s\n", sz);
	else if (Direction == USBD_TRANSFER_DIRECTION_OUT && DEBUG_DATA_OUT)
		DbgPrint("DATA OUT:%s\n", sz);

}

//
// Collection object is used to store all the FilterDevice objects so
// that any event callback routine can easily walk thru the list and pick a
// specific instance of the device for filtering.
//
WDFCOLLECTION   FilterDeviceCollection;
WDFWAITLOCK     FilterDeviceCollectionLock;


#ifdef ALLOC_PRAGMA
#pragma alloc_text (INIT, DriverEntry)
#pragma alloc_text (PAGE, FilterEvtDeviceAdd)
#pragma alloc_text (PAGE, FilterEvtDeviceContextCleanup)
#endif


//
// ControlDevice provides a sideband communication to the filter from
// usermode. This is required if the filter driver is sitting underneath
// another driver that fails custom ioctls defined by the Filter driver.
// Since there is one control-device for all instances of the device the
// filter is attached to, we will store the device handle in a global variable.
//

WDFDEVICE       ControlDevice = NULL;

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, FilterEvtIoDeviceControl)
#pragma alloc_text (PAGE, FilterCreateControlDevice)
#pragma alloc_text (PAGE, FilterDeleteControlDevice)
#pragma alloc_text (PAGE, FilterEvtIoInternalDeviceControl)
#endif

NTSTATUS
DriverEntry(
    IN PDRIVER_OBJECT  DriverObject,
    IN PUNICODE_STRING RegistryPath
    )
/*++

Routine Description:

    Installable driver initialization entry point.
    This entry point is called directly by the I/O system.

Arguments:

    DriverObject - pointer to the driver object

    RegistryPath - pointer to a unicode string representing the path,
                   to driver-specific key in the registry.

Return Value:

    STATUS_SUCCESS if successful,
    STATUS_UNSUCCESSFUL otherwise.

--*/
{
    WDF_DRIVER_CONFIG   config;
    NTSTATUS            status;
    WDFDRIVER           hDriver;

    KdPrint(("SiriRemote Lower Filter Driver - DriverEntry.\n"));

    //
    // Initialize driver config to control the attributes that
    // are global to the driver. Note that framework by default
    // provides a driver unload routine. If you create any resources
    // in the DriverEntry and want to be cleaned in driver unload,
    // you can override that by manually setting the EvtDriverUnload in the
    // config structure. In general xxx_CONFIG_INIT macros are provided to
    // initialize most commonly used members.
    //

    WDF_DRIVER_CONFIG_INIT(
        &config,
        FilterEvtDeviceAdd
    );

    //
    // Create a framework driver object to represent our driver.
    //
    status = WdfDriverCreate(DriverObject,
                            RegistryPath,
                            WDF_NO_OBJECT_ATTRIBUTES, // Driver Attributes
                            &config, // Driver Config Info
                            &hDriver);

    if (!NT_SUCCESS(status)) {
        KdPrint( ("WdfDriverCreate failed with status 0x%x\n", status));
    }

    //
    // Since there is only one control-device for all the instances
    // of the physical device, we need an ability to get to particular instance
    // of the device in our FilterEvtIoDeviceControlForControl. For that we
    // will create a collection object and store filter device objects.        
    // The collection object has the driver object as a default parent.
    //

    status = WdfCollectionCreate(WDF_NO_OBJECT_ATTRIBUTES,
                                &FilterDeviceCollection);
    if (!NT_SUCCESS(status))
    {
        KdPrint( ("WdfCollectionCreate failed with status 0x%x\n", status));
        return status;
    }

    //
    // The wait-lock object has the driver object as a default parent.
    //

    status = WdfWaitLockCreate(WDF_NO_OBJECT_ATTRIBUTES,
                                &FilterDeviceCollectionLock);
    if (!NT_SUCCESS(status))
    {
        KdPrint( ("WdfWaitLockCreate failed with status 0x%x\n", status));
        return status;
    }
    
    return status;
}

NTSTATUS
FilterEvtDeviceAdd(
    IN WDFDRIVER        Driver,
    IN PWDFDEVICE_INIT  DeviceInit
    )
/*++
Routine Description:

    EvtDeviceAdd is called by the framework in response to AddDevice
    call from the PnP manager. Here you can query the device properties
    using WdfFdoInitWdmGetPhysicalDevice/IoGetDeviceProperty and based
    on that, decide to create a filter device object and attach to the
    function stack. If you are not interested in filtering this particular
    instance of the device, you can just return STATUS_SUCCESS without creating
    a framework device.

Arguments:

    Driver - Handle to a framework driver object created in DriverEntry

    DeviceInit - Pointer to a framework-allocated WDFDEVICE_INIT structure.

Return Value:

    NTSTATUS

--*/
{

	KdPrint(("SiriRemote Lower Filter Driver - FilterEvtDeviceAdd.\n"));

    WDF_OBJECT_ATTRIBUTES   deviceAttributes;
    PFILTER_EXTENSION       filterExt;
    NTSTATUS                status;
    WDFDEVICE               device;
    WDF_IO_QUEUE_CONFIG     ioQueueConfig;

    PAGED_CODE ();

    UNREFERENCED_PARAMETER(Driver);
    
    //
    // Tell the framework that you are filter driver. Framework
    // takes care of inheriting all the device flags & characteristics
    // from the lower device you are attaching to.
    //
    WdfFdoInitSetFilter(DeviceInit);

    //
    // Specify the size of device extension where we track per device
    // context.
    //

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, FILTER_EXTENSION);

    //
    // We will just register for cleanup notification because we have to
    // delete the control-device when the last instance of the device goes
    // away. If we don't delete, the driver wouldn't get unloaded automatically
    // by the PNP subsystem.
    //
    deviceAttributes.EvtCleanupCallback = FilterEvtDeviceContextCleanup;
    
    //
    // Create a framework device object. This call will in turn create
    // a WDM deviceobject, attach to the lower stack and set the
    // appropriate flags and attributes.
    //
    status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);
    if (!NT_SUCCESS(status)) {
        KdPrint( ("WdfDeviceCreate failed with status code 0x%x\n", status));
        return status;
    }

    filterExt = FilterGetData(device);

    //
    // Add this device to the FilterDevice collection.
    //
    WdfWaitLockAcquire(FilterDeviceCollectionLock, NULL);
    //
    // WdfCollectionAdd takes a reference on the item object and removes
    // it when you call WdfCollectionRemove.
    //
    status = WdfCollectionAdd(FilterDeviceCollection, device);
    if (!NT_SUCCESS(status)) {
        KdPrint( ("WdfCollectionAdd failed with status code 0x%x\n", status));
    }
    WdfWaitLockRelease(FilterDeviceCollectionLock);

    //
    // Create a control device
    //
    status = FilterCreateControlDevice(device);
    if (!NT_SUCCESS(status)) {
        KdPrint( ("FilterCreateControlDevice failed with status 0x%x\n",
                                status));
        //
        // Let us not fail AddDevice just because we weren't able to create the
        // control device.
        //
        status = STATUS_SUCCESS;
    }
    
    //
    // Configure the default queue to be Parallel. 
	// WdfIoQueueDispatchParallel means that we are capable of handling
	// all the I/O requests simultaneously and we are responsible for protecting
	// data that could be accessed by these callbacks simultaneously.
	// A default queue gets all the requests that are not
	// configured for forwarding using WdfDeviceConfigureRequestDispatching.
    //
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&ioQueueConfig,
                             WdfIoQueueDispatchParallel);

    //
    // Framework by default creates non-power managed queues for
    // filter drivers.
    //
	ioQueueConfig.EvtIoInternalDeviceControl = FilterEvtIoInternalDeviceControl;

    status = WdfIoQueueCreate(device,
                            &ioQueueConfig,
                            WDF_NO_OBJECT_ATTRIBUTES,
                            WDF_NO_HANDLE // pointer to default queue
                            );
    if (!NT_SUCCESS(status)) {
        KdPrint( ("WdfIoQueueCreate failed 0x%x\n", status));
        return status;
    }

    return status;
}

NTSTATUS
FilterCreateControlDevice(
	WDFDEVICE Device
    )
/*++

Routine Description:

	This routine is called to create a control deviceobject so that application
	can talk to the filter driver directly instead of going through the entire
	device stack. This kind of control device object is useful if the filter
	driver is underneath another driver which prevents ioctls not known to it
	or if the driver's dispatch routine is owned by some other (port/class)
	driver and it doesn't allow any custom ioctls.

	NOTE: Since the control device is global to the driver and accessible to
	all instances of the device this filter is attached to, we create only once
	when the first instance of the device is started and delete it when the
	last instance gets removed.

Arguments:

	Device - Handle to a filter device object.

Return Value:

	WDF status code

--*/
{
	PWDFDEVICE_INIT             pInit = NULL;
	WDFDEVICE                   controlDevice = NULL;
	WDF_OBJECT_ATTRIBUTES       controlAttributes;
	WDF_IO_QUEUE_CONFIG         ioQueueConfig;
	BOOLEAN                     bCreate = FALSE;
	NTSTATUS                    status;
	WDFQUEUE                    queue;
	DECLARE_CONST_UNICODE_STRING(ntDeviceName, NTDEVICE_NAME_STRING);
	DECLARE_CONST_UNICODE_STRING(symbolicLinkName, SYMBOLIC_NAME_STRING);

	PAGED_CODE();

	//
	// First find out whether any ControlDevice has been created. If the
	// collection has more than one device then we know somebody has already
	// created or in the process of creating the device.
	//
	WdfWaitLockAcquire(FilterDeviceCollectionLock, NULL);

	if (WdfCollectionGetCount(FilterDeviceCollection) == 1) {
		bCreate = TRUE;
	}

	WdfWaitLockRelease(FilterDeviceCollectionLock);

	if (!bCreate) {
		//
		// Control device is already created. So return success.
		//
		return STATUS_SUCCESS;
	}

	KdPrint(("Creating Control Device\n"));

	//
	//
	// In order to create a control device, we first need to allocate a
	// WDFDEVICE_INIT structure and set all properties.
	//
	pInit = WdfControlDeviceInitAllocate(
		WdfDeviceGetDriver(Device),
		&SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RW_RES_R
	);

	if (pInit == NULL) {
		status = STATUS_INSUFFICIENT_RESOURCES;
		goto Error;
	}

	//
	// Set exclusive to false so that more than one app can talk to the
	// control device simultaneously.
	//
	WdfDeviceInitSetExclusive(pInit, FALSE);

	status = WdfDeviceInitAssignName(pInit, &ntDeviceName);

	if (!NT_SUCCESS(status)) {
		goto Error;
	}

	//
	// Specify the size of device context
	//
	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&controlAttributes,
		CONTROL_DEVICE_EXTENSION);
	status = WdfDeviceCreate(&pInit,
		&controlAttributes,
		&controlDevice);
	if (!NT_SUCCESS(status)) {
		goto Error;
	}

	//
	// Create a symbolic link for the control object so that usermode can open
	// the device.
	//
	status = WdfDeviceCreateSymbolicLink(controlDevice,
		&symbolicLinkName);

	if (!NT_SUCCESS(status)) {
		goto Error;
	}

	//
    // Configure the default queue associated with the control device object
	// to be Serial so that request passed to EvtIoDeviceControl are serialized.
	//
	WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&ioQueueConfig,
		WdfIoQueueDispatchSequential);

	ioQueueConfig.EvtIoDeviceControl = FilterEvtIoDeviceControl;

	//
	// Framework by default creates non-power managed queues for
	// filter drivers.
	//
	status = WdfIoQueueCreate(controlDevice,
		&ioQueueConfig,
		WDF_NO_OBJECT_ATTRIBUTES,
		&queue // pointer to default queue
	);
	if (!NT_SUCCESS(status)) {
		goto Error;
	}

	//
	// Control devices must notify WDF when they are done initializing.   I/O is
	// rejected until this call is made.
	//
	WdfControlFinishInitializing(controlDevice);

	ControlDevice = controlDevice;

	return STATUS_SUCCESS;

Error:

	if (pInit != NULL) {
		WdfDeviceInitFree(pInit);
	}

	if (controlDevice != NULL) {
		//
		// Release the reference on the newly created object, since
		// we couldn't initialize it.
		//
		WdfObjectDelete(controlDevice);
		controlDevice = NULL;
	}

	return status;
}

VOID
FilterEvtDeviceContextCleanup(
    WDFOBJECT Device
    )
/*++

Routine Description:

   EvtDeviceRemove event callback must perform any operations that are
   necessary before the specified device is removed. The framework calls
   the driver's EvtDeviceRemove callback when the PnP manager sends
   an IRP_MN_REMOVE_DEVICE request to the driver stack.

Arguments:

    Device - Handle to a framework device object.

Return Value:

    WDF status code

--*/
{
    ULONG   count;

    PAGED_CODE();

    KdPrint(("SiriRemote Lower Filter Driver - FilterEvtDeviceContextCleanup\n"));

    WdfWaitLockAcquire(FilterDeviceCollectionLock, NULL);

    count = WdfCollectionGetCount(FilterDeviceCollection);

    if(count == 1)
    {
         //
         // We are the last instance. So let us delete the control-device
         // so that driver can unload when the FilterDevice is deleted.
         // We absolutely have to do the deletion of control device with
         // the collection lock acquired because we implicitly use this
         // lock to protect ControlDevice global variable. We need to make
         // sure another thread doesn't attempt to create while we are
         // deleting the device.
         //
         FilterDeleteControlDevice((WDFDEVICE)Device);
     }

    WdfCollectionRemove(FilterDeviceCollection, Device);

    WdfWaitLockRelease(FilterDeviceCollectionLock);
}

VOID
FilterDeleteControlDevice(
    WDFDEVICE Device
    )
/*++

Routine Description:

    This routine deletes the control by doing a simple dereference.

Arguments:

    Device - Handle to a framework filter device object.

Return Value:

    WDF status code

--*/
{
    UNREFERENCED_PARAMETER(Device);

    PAGED_CODE();

    KdPrint(("Deleting Control Device\n"));

    if (ControlDevice) {
        WdfObjectDelete(ControlDevice);
        ControlDevice = NULL;
    }
}

VOID
FilterEvtIoDeviceControl(
    IN WDFQUEUE      Queue,
    IN WDFREQUEST    Request,
    IN size_t        OutputBufferLength,
    IN size_t        InputBufferLength,
    IN ULONG         IoControlCode
    )
/*++

Routine Description:

    This event is called when the framework receives IRP_MJ_DEVICE_CONTROL
    requests from the system.
    
Arguments:

    Queue - Handle to the framework queue object that is associated
            with the I/O request.
    Request - Handle to a framework request object.

    OutputBufferLength - length of the request's output buffer,
                        if an output buffer is available.
    InputBufferLength - length of the request's input buffer,
                        if an input buffer is available.

    IoControlCode - the driver-defined or system-defined I/O control code
                    (IOCTL) that is associated with the request.

Return Value:

   VOID

--*/
{
#define IOCTL_FIX_HCI_L2CAP_HEADERS_ON CTL_CODE(FILE_DEVICE_UNKNOWN, 0X11, METHOD_BUFFERED, FILE_READ_DATA)
#define IOCTL_FIX_HCI_L2CAP_HEADERS_OFF CTL_CODE(FILE_DEVICE_UNKNOWN, 0X10, METHOD_BUFFERED, FILE_READ_DATA)
#define IOCTL_DEBUG_DATA_IN_ON CTL_CODE(FILE_DEVICE_UNKNOWN, 0x21, METHOD_BUFFERED, FILE_READ_DATA)
#define IOCTL_DEBUG_DATA_IN_OFF CTL_CODE(FILE_DEVICE_UNKNOWN, 0x20, METHOD_BUFFERED, FILE_READ_DATA)
#define IOCTL_DEBUG_DATA_OUT_ON CTL_CODE(FILE_DEVICE_UNKNOWN, 0x31, METHOD_BUFFERED, FILE_READ_DATA)
#define IOCTL_DEBUG_DATA_OUT_OFF CTL_CODE(FILE_DEVICE_UNKNOWN, 0x30, METHOD_BUFFERED, FILE_READ_DATA)

    //ULONG					i;
    //ULONG					noItems;
    //WDFDEVICE				device;
	//WDFMEMORY				outputMemory;
    //PFILTER_EXTENSION		filterExt;
    NTSTATUS				status = STATUS_SUCCESS;
	size_t					bytesTransferred = 0;

    UNREFERENCED_PARAMETER(Queue);
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    PAGED_CODE();

    KdPrint(("SiriRemote Lower Filter Driver - FilterEvtIoDeviceControl.\n"));
    
    KdPrint(("Ioctl received into filter control object.\n"));

	switch (IoControlCode) {
	case IOCTL_FIX_HCI_L2CAP_HEADERS_ON:
		FIX_HCI_L2CAP_HEADERS = TRUE;
		break;
	case IOCTL_FIX_HCI_L2CAP_HEADERS_OFF:
		FIX_HCI_L2CAP_HEADERS = FALSE;
		break;
	case IOCTL_DEBUG_DATA_IN_ON:
		DEBUG_DATA_IN = TRUE;
		break;
	case IOCTL_DEBUG_DATA_IN_OFF:
		DEBUG_DATA_IN = FALSE;
		break;
	case IOCTL_DEBUG_DATA_OUT_ON:
		DEBUG_DATA_OUT = TRUE;
		break;
	case IOCTL_DEBUG_DATA_OUT_OFF:
		DEBUG_DATA_OUT = FALSE;
		break;
	default:
		status = STATUS_NOT_IMPLEMENTED; //Or STATUS_INVALID_DEVICE_REQUEST;
		break;
	}

	/*
    WdfWaitLockAcquire(FilterDeviceCollectionLock, NULL);

    noItems = WdfCollectionGetCount(FilterDeviceCollection);

    for(i=0; i<noItems ; i++) {
        device = WdfCollectionGetItem(FilterDeviceCollection, i);

        filterExt = FilterGetData(device);

        KdPrint(("filterExt WdfCollectionGetItem: %d\n", i));
    }

    WdfWaitLockRelease(FilterDeviceCollectionLock);
	*/

	//
	// Complete the Request.
	//
    WdfRequestCompleteWithInformation(Request, status, bytesTransferred);

    return;
}

VOID
FilterEvtIoInternalDeviceControl(
	IN WDFQUEUE      Queue,
	IN WDFREQUEST    Request,
	IN size_t        OutputBufferLength,
	IN size_t        InputBufferLength,
	IN ULONG         IoControlCode
)
{
	PFILTER_EXTENSION               filterExt;
	NTSTATUS                        status = STATUS_SUCCESS;
	WDFDEVICE                       device;

	UNREFERENCED_PARAMETER(OutputBufferLength);
	UNREFERENCED_PARAMETER(InputBufferLength);

	KdPrint(("SiriRemote Lower Filter Driver - FilterEvtIoInternalDeviceControl.\n"));

	device = WdfIoQueueGetDevice(Queue);

	filterExt = FilterGetData(device);

	switch (IoControlCode) {
		//
		// Put your cases for handling IOCTLs here
		//
		case IOCTL_INTERNAL_USB_SUBMIT_URB:
		{
			PURB pUrb;

			pUrb = (PURB)IoGetCurrentIrpStackLocation(WdfRequestWdmGetIrp(Request))->Parameters.Others.Argument1;

			KdPrint(("URB_FUNCTION: %x\n", pUrb->UrbHeader.Function));

			switch (pUrb->UrbHeader.Function)
			{
			case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER: {

				KdPrint(("URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER\n"));

				KdPrint(("Urb @ 0x%08x (header length: %d, function: %04x)!\n", pUrb, pUrb->UrbHeader.Length, pUrb->UrbHeader.Function));

				struct _URB_BULK_OR_INTERRUPT_TRANSFER *pBulkOrInterruptTransfer = (struct _URB_BULK_OR_INTERRUPT_TRANSFER *) pUrb;

				KdPrint(("TransferFlags: %lu\n", pBulkOrInterruptTransfer->TransferFlags));
				KdPrint(("TransferBufferLength: %lu\n", pBulkOrInterruptTransfer->TransferBufferLength));

				BOOLEAN bReadFromDevice = (BOOLEAN)(pBulkOrInterruptTransfer->TransferFlags & USBD_TRANSFER_DIRECTION_IN);

				//Direction Out
				if (!bReadFromDevice)
				{
					//CopyTransferBuffer(pDest, uUrbUserDataSize,
					//	(PUCHAR)pBulkOrInterruptTransfer->TransferBuffer,
					//	pBulkOrInterruptTransfer->TransferBufferMDL,
					//	pBulkOrInterruptTransfer->TransferBufferLength);

					if ((PUCHAR)pBulkOrInterruptTransfer->TransferBuffer)
					{
						KdPrint(("TransferBuffer has flat specified.\n"));

						if (pBulkOrInterruptTransfer->TransferBufferMDL)
						{
							KdPrint(("??? weird transfer buffer, both MDL and flat specified. Ignoring MDL\n"));
						}

						/*
						if (pBulkOrInterruptTransfer->TransferBufferLength == 15)
						{
							//using C:\Program Files (x86)\Windows Kits\10\Tools\x86\Bluetooth\btvs
							//----HCI---- ----L2CAP-- -------ATT----------
							//80 00 0b 00 07 00 04 00 08 26 00 2c 00 03 28
							//ATT: starting handle 0x26, ending handle 0x2c this is for the batter service
							//we will change this to 0x15 and 0x25 to get back hid charistristics

							unsigned char * Bfr = (PUCHAR)pBulkOrInterruptTransfer->TransferBuffer;

							if(	Bfr[0]==0x80 && 
								Bfr[1] == 0x00 && 
								Bfr[2] == 0x0b &&
								Bfr[3] == 0x00 && 
								Bfr[4] == 0x07 && 
								Bfr[5] == 0x00 && 
								Bfr[6] == 0x04 && 
								Bfr[7] == 0x00 &&
								Bfr[8] == 0x08 && 
								Bfr[9] == 0x26 && 
								Bfr[10] == 0x00 && 
								Bfr[11] == 0x2c && 
								Bfr[12] == 0x00 && 
								Bfr[13] == 0x03 && 
								Bfr[14] == 0x28)
							{
								KdPrint(("Making changes to get hid attributes\n"));
								Bfr[9] = 0x15;
								Bfr[11] = 0x25;
							}
						}
						*/

						if (pBulkOrInterruptTransfer->TransferBufferLength == 12)
						{
							//intercept a write with no response request and replace with our write
							//---HCI----- ---L2CAP--- ----ATT----
							//80 00 08 00 04 00 04 00 52 28 00 AF

							unsigned char * Bfr = (PUCHAR)pBulkOrInterruptTransfer->TransferBuffer;

							if (Bfr[0] == 0x80 &&
								Bfr[1] == 0x00 &&
								Bfr[2] == 0x08 &&
								Bfr[3] == 0x00 &&
								Bfr[4] == 0x04 &&
								Bfr[5] == 0x00 &&
								Bfr[6] == 0x04 &&
								Bfr[7] == 0x00 &&
								Bfr[8] == 0x52 &&
								Bfr[9] == 0x28 &&
								Bfr[10] == 0x00 &&
								Bfr[11] == 0xAF)
							{
								KdPrint(("Writing 0xAF (magic value) to att handle 0x1d (hid att handle)\n"));
								Bfr[8] = 0x12; //change to write request from 0x52 (write without response)
								Bfr[9] = 0x1d; //change att handle from 0x28 to 0x1d
							}
						}

						if (pBulkOrInterruptTransfer->TransferBufferLength == 13)
						{
							//intercept a write with response request and replace with our write
							//---HCI----- ---L2CAP--- -----ATT------
							//80 00 09 00 05 00 04 00 12 29 00 01 00

							unsigned char * Bfr = (PUCHAR)pBulkOrInterruptTransfer->TransferBuffer;

							if (Bfr[0] == 0x80 &&
								Bfr[1] == 0x00 &&
								Bfr[2] == 0x09 &&
								Bfr[3] == 0x00 &&
								Bfr[4] == 0x05 &&
								Bfr[5] == 0x00 &&
								Bfr[6] == 0x04 &&
								Bfr[7] == 0x00 &&
								Bfr[8] == 0x12 &&
								Bfr[9] == 0x29 &&
								Bfr[10] == 0x00 &&
								Bfr[11] == 0x01 &&
								Bfr[12] == 0x00)
							{
								KdPrint(("Writing 0x0100 to att handle 0x24 (hid notify)\n"));
								Bfr[9] = 0x24; //change att handle from 0x29 to 0x24
							}
						}


						/*
						if (pBulkOrInterruptTransfer->TransferBufferLength == 11)
						{
							//intercept a mtu request
							//---HCI----- ---L2CAP--- --ATT---
							//80 00 07 00 03 00 04 00 02 0d 02

							unsigned char * Bfr = (PUCHAR)pBulkOrInterruptTransfer->TransferBuffer;

							if (Bfr[0] == 0x80 &&
								Bfr[1] == 0x00 &&
								Bfr[2] == 0x07 &&
								Bfr[3] == 0x00 &&
								Bfr[4] == 0x03 &&
								Bfr[5] == 0x00 &&
								Bfr[6] == 0x04 &&
								Bfr[7] == 0x00 &&
								Bfr[8] == 0x02 &&
								Bfr[9] == 0x0d &&
								Bfr[10] == 0x02)
							{
								KdPrint(("Changing mtu request from 525 0x20D to 104 0x68\n"));
								Bfr[9] = 0x68;
								Bfr[10] = 0x00;
							}
						}
						*/

						Dump(USBD_TRANSFER_DIRECTION_OUT, (PUCHAR)pBulkOrInterruptTransfer->TransferBuffer, pBulkOrInterruptTransfer->TransferBufferLength);
					}
					else if (pBulkOrInterruptTransfer->TransferBufferMDL)
					{
						KdPrint(("TransferBuffer has MDL specified.\n"));

						#pragma warning(disable : 4995) //MmGetSystemAddressForMdl is deprecated so ignore warning
						PUCHAR pMDLBuf = (PUCHAR)MmGetSystemAddressForMdl(pBulkOrInterruptTransfer->TransferBufferMDL);
						if (pMDLBuf)
						{
							Dump(USBD_TRANSFER_DIRECTION_OUT, pMDLBuf, pBulkOrInterruptTransfer->TransferBufferLength);
						}
						else
						{
							KdPrint(("Could not get MmGetSystemAddressForMdl.\n"));
						}
					}
					else
					{
						KdPrint(("Both flat and MDL not specified.\n"));
					}

				}
				else
				{
					KdPrint(("NoTransferBuffer\n"));
				}


				break;
			}
			case URB_FUNCTION_CLASS_DEVICE: {
				// My code Here
				KdPrint(("URB_FUNCTION_CLASS_DEVICE\n"));
				break;
			}
			default:
				KdPrint(("URB_FUNCTION_UNKNOWN\n"));
				break;
			}
		}
		default:
			//status = STATUS_INVALID_DEVICE_REQUEST;
			break;
	}

	if (!NT_SUCCESS(status)) {
		WdfRequestComplete(Request, status);
		return;
	}

	//
	// Forward the request down. WdfDeviceGetIoTarget returns
	// the default target, which represents the device attached to us below in
	// the stack.
	//
#if FORWARD_REQUEST_WITH_COMPLETION
	//
	// Use this routine to forward a request if you are interested in post
	// processing the IRP.
	//
	FilterForwardRequestWithCompletionRoutine(Request,
		WdfDeviceGetIoTarget(device));
#else
	FilterForwardRequest(Request, WdfDeviceGetIoTarget(device));
#endif

	return;
}

VOID
FilterForwardRequest(
    IN WDFREQUEST Request,
    IN WDFIOTARGET Target
    )
/*++
Routine Description:

    Passes a request on to the lower driver.

--*/
{
    WDF_REQUEST_SEND_OPTIONS options;
    BOOLEAN ret;
    NTSTATUS status;

    //
    // We are not interested in post processing the IRP so 
    // fire and forget.
    //
    WDF_REQUEST_SEND_OPTIONS_INIT(&options,
                                  WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);

    ret = WdfRequestSend(Request, Target, &options);

    if (ret == FALSE) {
        status = WdfRequestGetStatus (Request);
        KdPrint( ("WdfRequestSend failed: 0x%x\n", status));
        WdfRequestComplete(Request, status);
    }

    return;
}

#if FORWARD_REQUEST_WITH_COMPLETION

VOID
FilterForwardRequestWithCompletionRoutine(
    IN WDFREQUEST Request,
    IN WDFIOTARGET Target
    )
/*++
Routine Description:

    This routine forwards the request to a lower driver with
    a completion so that when the request is completed by the
    lower driver, it can regain control of the request and look
    at the result.

--*/
{
    BOOLEAN ret;
    NTSTATUS status;

    //
    // The following funciton essentially copies the content of
    // current stack location of the underlying IRP to the next one. 
    //
    WdfRequestFormatRequestUsingCurrentType(Request);

    WdfRequestSetCompletionRoutine(Request,
                                FilterRequestCompletionRoutine,
                                WDF_NO_CONTEXT);

    ret = WdfRequestSend(Request,
                         Target,
                         WDF_NO_SEND_OPTIONS);

    if (ret == FALSE) {
        status = WdfRequestGetStatus (Request);
        KdPrint( ("WdfRequestSend failed: 0x%x\n", status));
        WdfRequestComplete(Request, status);
    }

    return;
}

VOID
FilterRequestCompletionRoutine(
    IN WDFREQUEST                  Request,
    IN WDFIOTARGET                 Target,
    PWDF_REQUEST_COMPLETION_PARAMS CompletionParams,
    IN WDFCONTEXT                  Context
   )
/*++

Routine Description:

    Completion Routine

Arguments:

    Target - Target handle
    Request - Request handle
    Params - request completion params
    Context - Driver supplied context


Return Value:

    VOID

--*/
{
    UNREFERENCED_PARAMETER(Target);
    UNREFERENCED_PARAMETER(Context);

	//WDFMEMORY   buffer = CompletionParams->Parameters.Ioctl.Output.Buffer;
	NTSTATUS    status = CompletionParams->IoStatus.Status;

	KdPrint(("SiriRemote Lower Filter Driver - FilterRequestCompletionRoutine"));

	PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(WdfRequestWdmGetIrp(Request));

	//KdPrint(("CompletionParams->Type: %x\n", CompletionParams->Type)); //Types: WdfRequestTypeDeviceControlInternal
	//KdPrint(("Parameters.Ioctl.Output.Length: %d\n", CompletionParams->Parameters.Ioctl.Output.Length));
	//KdPrint(("Parameters.Ioctl.IoControlCode: %x (%lu)\n", CompletionParams->Parameters.Ioctl.IoControlCode, CompletionParams->Parameters.Ioctl.IoControlCode));
	//KdPrint(("stack->Parameters.DeviceIoControl.IoControlCode: %x (%lu)\n", stack->Parameters.DeviceIoControl.IoControlCode, stack->Parameters.DeviceIoControl.IoControlCode));

	//CompletionParams->Parameters.Ioctl.IoControlCode
	if (NT_SUCCESS(status) &&
		stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_INTERNAL_USB_SUBMIT_URB) {

		PURB pUrb;

		pUrb = (PURB)stack->Parameters.Others.Argument1;

		//KdPrint(("URB_FUNCTION: %x\n", pUrb->UrbHeader.Function));

		switch (pUrb->UrbHeader.Function)
		{
		case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER: {

			KdPrint(("URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER\n"));

			KdPrint(("Urb @ 0x%08x (header length: %d, function: %04x)!\n", pUrb, pUrb->UrbHeader.Length, pUrb->UrbHeader.Function));

			struct _URB_BULK_OR_INTERRUPT_TRANSFER *pBulkOrInterruptTransfer = (struct _URB_BULK_OR_INTERRUPT_TRANSFER *) pUrb;

			KdPrint(("TransferFlags: %lu\n", pBulkOrInterruptTransfer->TransferFlags));
			KdPrint(("TransferBufferLength: %lu\n", pBulkOrInterruptTransfer->TransferBufferLength));

			BOOLEAN bReadFromDevice = (BOOLEAN)(pBulkOrInterruptTransfer->TransferFlags & USBD_TRANSFER_DIRECTION_IN);

			//Direction In
			if (bReadFromDevice)
			{
				if ((PUCHAR)pBulkOrInterruptTransfer->TransferBuffer)
				{
					KdPrint(("TransferBuffer has flat specified.\n"));

					if (pBulkOrInterruptTransfer->TransferBufferMDL)
					{
						KdPrint(("??? weird transfer buffer, both MDL and flat specified. Ignoring MDL\n"));
					}

					if (pBulkOrInterruptTransfer->TransferBufferLength <= 24)
					{
						//intercept a HID Notify and replace with a BatteryPowerState Notify
						//this way we can get back hid notifications under the battery service
						//in the userland console application.
						//we do all this because hid service is restricted by the system.
						//---HCI----- ---L2CAP--- -----ATT------
						//80 20 09 00 05 00 04 00 1b 23 00 00 02 (button press)
						//Or
						//80 20 14 00 10 00 04 00 1b 23 00 01 00 32 a2 4d 09 e6 18 ca 8a 07 02 a2 (trackpad touch/move)


						unsigned char * Bfr = (PUCHAR)pBulkOrInterruptTransfer->TransferBuffer;

						if (Bfr[0] == 0x80 &&
							Bfr[1] == 0x20 &&
							//Bfr[2] == 0x09 &&
							Bfr[3] == 0x00 &&
							//Bfr[4] == 0x05 &&
							Bfr[5] == 0x00 &&
							Bfr[6] == 0x04 &&
							Bfr[7] == 0x00 &&
							Bfr[8] == 0x1b &&
							Bfr[9] == 0x23 &&
							Bfr[10] == 0x00)
						{
							KdPrint(("Changing att handle from 0x23 (hid notify) to 0x2b (BatterPowerState Notify)\n"));
							Bfr[9] = 0x2b; //change att handle from 0x23 (hid notify) to 0x2b (BatterPowerState Notify)
						}

						DumpSingleLine(USBD_TRANSFER_DIRECTION_IN, (PUCHAR)pBulkOrInterruptTransfer->TransferBuffer, pBulkOrInterruptTransfer->TransferBufferLength);
					}

					//Bug in ble adapter/system
					//The lmp version of the bluetooth device on the test computer is 6.37942 which is ble version 4.0
					//which means it only supports max packet size of 23 bytes and there is no way around this, even when 
					//setting the mtu higher in the initial exchange.
					//If we dont intervene the first voice packet after pressing the voice button stops hid notifications, 
					//(it must be hanging something in the upper stack). So we update the packet headers for HCI and L2CAP
					//accordingly.
					//This seems to work, as it allows the hid notifications to flow including all voice data.
					//If we dont set TransferBufferLength we seem to be getting the full data in DebugView bypassing this 
					//ble 4.0 lme limitation??? However at the console applications if we dont set TransferBufferLength
					//the voice notifications dont come through
					else if (pBulkOrInterruptTransfer->TransferBufferLength > 30)
					{
						unsigned char * Bfr = (PUCHAR)pBulkOrInterruptTransfer->TransferBuffer;

						if (Bfr[0] == 0x80 &&
							Bfr[1] == 0x20 &&
							//Bfr[2] == 0x09 &&
							Bfr[3] == 0x00 &&
							//Bfr[4] == 0x05 &&
							Bfr[5] == 0x00 &&
							Bfr[6] == 0x04 &&
							Bfr[7] == 0x00 &&
							Bfr[8] == 0x1b &&
							Bfr[9] == 0x23 &&
							Bfr[10] == 0x00)
						{
							if (FIX_HCI_L2CAP_HEADERS)
							{
								KdPrint(("Fixing HCI, L2CAP headers.\n"));
								Bfr[2] = 0x1A; //in HCI max 26 chars for l2cap + att
								Bfr[4] = 0x16; //in L2CAP max 22 chars for att
							}

							Bfr[9] = 0x2b; //change att handle from 0x23 (hid notify) to 0x2b (BatterPowerState Notify)

							//Dump to debug before modifying TransferBufferLength for the upper stack, 
							//this way we can at least pull the voice data from DebugView
							DumpSingleLine(USBD_TRANSFER_DIRECTION_IN, (PUCHAR)pBulkOrInterruptTransfer->TransferBuffer, pBulkOrInterruptTransfer->TransferBufferLength);

							if (FIX_HCI_L2CAP_HEADERS)
								pBulkOrInterruptTransfer->TransferBufferLength = 30;
						}
					}
					else
					{
						Dump(USBD_TRANSFER_DIRECTION_IN, (PUCHAR)pBulkOrInterruptTransfer->TransferBuffer, pBulkOrInterruptTransfer->TransferBufferLength);
					}
				}
				else if (pBulkOrInterruptTransfer->TransferBufferMDL)
				{
					KdPrint(("TransferBuffer has MDL specified.\n"));

					#pragma warning(disable : 4995) //MmGetSystemAddressForMdl is deprecated so ignore warning
					PUCHAR pMDLBuf = (PUCHAR)MmGetSystemAddressForMdl(pBulkOrInterruptTransfer->TransferBufferMDL);
					if (pMDLBuf)
					{
						Dump(USBD_TRANSFER_DIRECTION_IN, pMDLBuf, pBulkOrInterruptTransfer->TransferBufferLength);
					}
					else
					{
						KdPrint(("Could not get MmGetSystemAddressForMdl.\n"));
					}
				}
				else
				{
					KdPrint(("Both flat and MDL not specified.\n"));
				}

			}
			else
			{
				KdPrint(("NoTransferBuffer\n"));
			}

			break;
		}
		case URB_FUNCTION_CLASS_DEVICE: {
			// My code Here
			KdPrint(("URB_FUNCTION_CLASS_DEVICE\n"));
			break;
		}
		default:
			KdPrint(("URB_FUNCTION_UNKNOWN\n"));
			break;
		}
	}

    WdfRequestComplete(Request, CompletionParams->IoStatus.Status);

    return;
}

#endif //FORWARD_REQUEST_WITH_COMPLETION








