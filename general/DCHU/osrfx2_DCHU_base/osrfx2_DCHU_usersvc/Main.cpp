/*++

Copyright (c) Microsoft Corporation.  All rights reserved.

    THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY
    KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR
    PURPOSE.

Module Name:

    Main.cpp

Abstract:

    Implements the functions to control the OSR USB FX2 device.

Environment:

    User mode

--*/

#include "Main.h"
#include "Utils.h"

//
// Keep track of where the OSRFX2 device's bar graph currently is.
//
INT             CurrentBar;
BAR_GRAPH_STATE BarGraphState;

/*++

Routine Description:

    Sets the variables in this service to their default values.

Arguments:

    VOID

Return Value:

    VOID

--*/
VOID
SetVariables()
{
    CurrentBar = 0;
}


/*++

Routine Description:

    Callback for when a device is being query removed.

Arguments:

    Context - The callback context

Return Value:

    NULL

--*/
VOID
DeviceQueryRemoveAction(
    _In_ PDEVICE_CONTEXT Context
    )
{
    //
    // Remove device from device list
    //
    AcquireSRWLockExclusive(&DeviceListLock);
    RemoveDeviceListEntry(&Context->ListEntry);
    ReleaseSRWLockExclusive(&DeviceListLock);

    Context->ListEntry.Flink = Context->ListEntry.Blink = NULL;

    if (Context->DeviceHandle != INVALID_HANDLE_VALUE)
    {
        //
        // Close open handles to allow the device to exit
        //
        CloseHandle(Context->DeviceHandle);

        Context->DeviceHandle = INVALID_HANDLE_VALUE;
    }
}


/*++

Routine Description:

    Handles a device query remove failed notification.

Arguments:

    hNotify - The notification that spurred this callback

    Context - The callback context

Return Value:

    NULL

--*/
VOID
DeviceQueryRemoveFailedAction(
    _In_ PDEVICE_CONTEXT Context
    )
{
    DWORD Err                 = ERROR_SUCCESS;
    PWSTR DeviceInterfacePath = NULL;

    //
    // Keep a record of device symbolic link since the callback context will
    // be freed up during unregistration
    //
    DeviceInterfacePath = Context->SymbolicLink;
    Context->SymbolicLink = NULL;

    //
    // Remove device from device list if it is still present
    //
    if (Context->ListEntry.Flink != NULL)
    {
        AcquireSRWLockExclusive(&DeviceListLock);
        RemoveDeviceListEntry(&Context->ListEntry);
        ReleaseSRWLockExclusive(&DeviceListLock);

        Context->ListEntry.Flink = Context->ListEntry.Blink = NULL;
    }

    //
    // Unregister notifications for the old device handle from a deferred
    // routine since CM_Unregister_Notification can not be called from a
    // notification callback
    //
    QueueUserWorkItem(UnregisterDeviceNotificationsWorkerThread,
                      Context,
                      WT_EXECUTEDEFAULT);

    //
    // Re-register for device notifications on a new device handle
    //
    Err = RegisterDeviceNotifications(DeviceInterfacePath);

    free(DeviceInterfacePath);
}

/*++

Routine Description:

    Handles a device remove complete notification.

Arguments:

    hNotify - The notification that spurred this callback

    Context - The callback context

Return Value:

    NULL

--*/
VOID
DeviceRemoveCompleteAction(
    _In_ PDEVICE_CONTEXT Context
    )
{
    //
    // Remove device from device list if it is still present
    //
    if (Context->ListEntry.Flink != NULL)
    {
        AcquireSRWLockExclusive(&DeviceListLock);
        RemoveDeviceListEntry(&Context->ListEntry);
        ReleaseSRWLockExclusive(&DeviceListLock);

        Context->ListEntry.Flink = Context->ListEntry.Blink = NULL;
    }

    //
    // Unregister notifications for the old device handle from a deferred
    // routine since CM_Unregister_Notification can not be called from a
    // notification callback
    //
    QueueUserWorkItem(UnregisterDeviceNotificationsWorkerThread,
                      Context,
                      WT_EXECUTEDEFAULT);
}

/*++

Routine Description:

    Handles device notifications.

Arguments:

    hNotify       - The notification that spurred this callback

    hContext      - The callback context

    Action        - The type of callback

    EventData     - Additional information about this callback

    EventDataSize - The size of EventData

Return Value:

    A Win32 error code.

--*/
DWORD
WINAPI
DeviceCallback(
    _In_ HCMNOTIFICATION       hNotify,
    _In_ PVOID                 hContext,
    _In_ CM_NOTIFY_ACTION      Action,
    _In_ PCM_NOTIFY_EVENT_DATA EventData,
    _In_ DWORD                 EventDataSize
    )
{
    PDEVICE_CONTEXT Context = (PDEVICE_CONTEXT)hContext;

    //
    // In case this callback fires before the registration call returns, make
    // sure the notification handle is properly set
    //
    Context->DeviceNotificationHandle = hNotify;

    switch (Action)
    {
    case CM_NOTIFY_ACTION_DEVICEQUERYREMOVE:
        DeviceQueryRemoveAction(Context);
        break;

    case CM_NOTIFY_ACTION_DEVICEQUERYREMOVEFAILED:
        DeviceQueryRemoveFailedAction(Context);
        break;

    case CM_NOTIFY_ACTION_DEVICEREMOVEPENDING:
    case CM_NOTIFY_ACTION_DEVICEREMOVECOMPLETE:
        //
        // Check if unregistration is being done in one of the callbacks
        //
        if (!Context->Unregistered)
        {
            Context->Unregistered = TRUE;
            DeviceRemoveCompleteAction(Context);
        }

        break;

    default:
        break;
    }

    return ERROR_SUCCESS;
}


/*++

Routine Description:

    Register for device notifications.

Arguments:

    DeviceInterfacePath - The symbolic link path of the device interface

Return Value:

    A Win32 error code.

--*/
DWORD
RegisterDeviceNotifications(
    _In_ PCWSTR DeviceInterfacePath
    )
{
    DWORD              Err          = ERROR_SUCCESS;
    CONFIGRET          Cr           = CR_SUCCESS;
    PDEVICE_CONTEXT    Context      = NULL;
    PDEVICE_LIST_ENTRY Link         = NULL;
    DWORD              BufferSize   = 0;
    CM_NOTIFY_FILTER   NotifyFilter = {0};

    //
    // Check whether the device interface has already been registered for device
    // handle notifications since it can be done either in interface arrival
    // callback routine or initial device interface setup routine
    //
    AcquireSRWLockShared(&DeviceListLock);

    for (Link = DeviceList.Flink; Link != &DeviceList; Link = Link->Flink)
    {
        Context = CONTAINING_RECORD(Link, DEVICE_CONTEXT, ListEntry);

        if (wcscmp(DeviceInterfacePath, Context->SymbolicLink) == 0)
        {
            break;
        }
    }

    ReleaseSRWLockShared(&DeviceListLock);

    if (Link != &DeviceList)
    {
        Err = ERROR_SUCCESS;
        goto cleanup;
    }

    //
    // Create a new device context
    //
    Context = (PDEVICE_CONTEXT)malloc(sizeof(DEVICE_CONTEXT));

    if (Context == NULL)
    {
        Err = ERROR_OUTOFMEMORY;
        goto cleanup;
    }

    //
    // Initialize device context
    //
    ZeroMemory(Context, sizeof(DEVICE_CONTEXT));
    Context->DeviceHandle = INVALID_HANDLE_VALUE;

    //
    // Fill out the context and register device handle notifications
    //
    BufferSize = wcslen(DeviceInterfacePath) + 1;
    Context->SymbolicLink = (PWSTR)malloc(BufferSize * sizeof(WCHAR));

    if ((Context->SymbolicLink == NULL) ||
        (FAILED(StringCchCopy(Context->SymbolicLink,
                              BufferSize,
                              DeviceInterfacePath))))
    {
        Err = ERROR_OUTOFMEMORY;
        goto cleanup;
    }

    Context->DeviceHandle = CreateFile(Context->SymbolicLink,
                                       GENERIC_WRITE | GENERIC_READ,
                                       FILE_SHARE_WRITE | FILE_SHARE_READ,
                                       NULL, // default security
                                       OPEN_EXISTING,
                                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
                                       NULL);

    if (Context->DeviceHandle == INVALID_HANDLE_VALUE)
    {
        Err = GetLastError();
        goto cleanup;
    }

    NotifyFilter.cbSize = sizeof(NotifyFilter);
    NotifyFilter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEHANDLE;
    NotifyFilter.u.DeviceHandle.hTarget = Context->DeviceHandle;

    Cr = CM_Register_Notification(&NotifyFilter,
                                  (PVOID)Context,
                                  (PCM_NOTIFY_CALLBACK)DeviceCallback,
                                  &Context->DeviceNotificationHandle);

    if (Cr != CR_SUCCESS)
    {
        Err = CM_MapCrToWin32Err(Cr, ERROR_INVALID_DATA);
        goto cleanup;
    }

    //
    // Add the device entry to device list
    //
    AcquireSRWLockExclusive(&DeviceListLock);
    InsertTailDeviceListEntry(&DeviceList, &Context->ListEntry);
    ReleaseSRWLockExclusive(&DeviceListLock);

    Context = NULL;

cleanup:

    //
    // Clean up device context if an error occurred.
    //
    if (Context != NULL)
    {
        if (Context->SymbolicLink != NULL)
        {
            free(Context->SymbolicLink);
        }

        if (Context->DeviceHandle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(Context->DeviceHandle);
        }
    }

    return Err;
}


VOID
UnregisterDeviceNotifications(
    _Inout_ PDEVICE_CONTEXT Context
    )
{
    if (Context->DeviceNotificationHandle != NULL)
    {
        CM_Unregister_Notification(Context->DeviceNotificationHandle);
    }

    if (Context->DeviceHandle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(Context->DeviceHandle);
    }

    if (Context->SymbolicLink != NULL)
    {
        free(Context->SymbolicLink);
    }

    free(Context);
}


/*++

Routine Description:

    This callback avoids a deadlock when unregistering device notifications.
    Rather than calling CM_Unregister_Notification from the callback, the
    callback gives that work to a separate thread to avoid deadlock.

Arguments:

    lpThreadParameter - The thread data passed to the function

Return Value:

    VOID

--*/
DWORD
WINAPI
UnregisterDeviceNotificationsWorkerThread(
    _In_ PVOID lpThreadParameter
    )
{
    PDEVICE_CONTEXT Context = (PDEVICE_CONTEXT)lpThreadParameter;

    UnregisterDeviceNotifications(Context);

    return 0;
}


/*++

Routine Description:

    Handles an interface arrival notification.

Arguments:

    hNotify       - The notification that fired the callback

    hContext      - The callback context

    Action        - The type of notification

    EventData     - Additional information about the callback

    EventDataSize - The size of EventData

Return Value:

    Notification callback should return ERROR_SUCCESS.

--*/
DWORD
WINAPI
InterfaceCallback(
    _In_ HCMNOTIFICATION       hNotify,
    _In_ PVOID                 hContext,
    _In_ CM_NOTIFY_ACTION      Action,
    _In_ PCM_NOTIFY_EVENT_DATA EventData,
    _In_ DWORD                 EventDataSize
    )
{
    switch (Action)
    {
    case CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL:
        //
        // Register for device notifications
        //
        RegisterDeviceNotifications(EventData->u.DeviceInterface.SymbolicLink);
        break;

    case CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL:
        //
        // Notify service to stop if this is the last device interface
        //
        AcquireSRWLockShared(&DeviceListLock);

        if (IsDeviceListEmpty(&DeviceList))
        {
            SetEvent(SvcStopRequestEvent);
        }

        ReleaseSRWLockShared(&DeviceListLock);
        break;
    default:
        break;
    }

    return ERROR_SUCCESS;
}


/*++

Routine Description:

    Registers the service for interface notifications

Arguments:

    pInterfaceNotificationHandle - Pointer to receive the HCMNOTIFICATION handle

Return Value:

    A Win32 error code.

--*/
DWORD
RegisterInterfaceNotifications(
    _Out_ PHCMNOTIFICATION pInterfaceNotificationHandle
    )
{
    DWORD            Err          = ERROR_SUCCESS;
    CONFIGRET        Cr           = CR_SUCCESS;
    CM_NOTIFY_FILTER NotifyFilter = {0};

    NotifyFilter.cbSize = sizeof(NotifyFilter);
    NotifyFilter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
    NotifyFilter.u.DeviceInterface.ClassGuid = GUID_DEVINTERFACE_OSRUSBFX2;

    //
    // Register for device interface events to open and close the handle to
    // the interface.
    //
    Cr = CM_Register_Notification(&NotifyFilter,
                                  NULL,
                                  (PCM_NOTIFY_CALLBACK)InterfaceCallback,
                                  pInterfaceNotificationHandle);

    if (Cr != CR_SUCCESS)
    {
        Err = CM_MapCrToWin32Err(Cr, ERROR_INVALID_DATA);
        goto cleanup;
    }

cleanup:

    return Err;
}


/*++

Routine Description:

    Setup device interface context by registering for device interface notifications

Arguments:

    NULL

Return Value:

    A Win32 error code.

--*/
DWORD
SetupDeviceInterfaceContext(
    VOID
    )
{
    DWORD     Err                     = ERROR_SUCCESS;
    CONFIGRET Cr                      = CR_SUCCESS;
    PCWSTR    DeviceInterface         = NULL;
    PWSTR     DeviceInterfaceList     = NULL;
    DWORD     DeviceInterfaceListSize = 0;

    //
    // Register for device interface events to open and close the handle to
    // the interface.
    //
    Err = RegisterInterfaceNotifications(&InterfaceNotificationHandle);

    if (Err != ERROR_SUCCESS)
    {
        goto cleanup;
    }

    //
    // The interface may already be present on the system. Retrieve a list of
    // existing interfaces
    //
    do
    {
        Cr = CM_Get_Device_Interface_List_Size(&DeviceInterfaceListSize,
                                               (LPGUID)&GUID_DEVINTERFACE_OSRUSBFX2,
                                               NULL,
                                               CM_GET_DEVICE_INTERFACE_LIST_PRESENT);

        if (Cr != CR_SUCCESS)
        {
            break;
        }

        if (DeviceInterfaceList != NULL)
        {
            free(DeviceInterfaceList);
        }

        DeviceInterfaceList = (PWSTR)malloc(DeviceInterfaceListSize * sizeof(WCHAR));

        if (DeviceInterfaceList == NULL)
        {
            Cr = CR_OUT_OF_MEMORY;
            break;
        }

        Cr = CM_Get_Device_Interface_List((LPGUID)&GUID_DEVINTERFACE_OSRUSBFX2,
                                          NULL,
                                          DeviceInterfaceList,
                                          DeviceInterfaceListSize,
                                          CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
    } while (Cr == CR_BUFFER_SMALL);

    if (Cr != CR_SUCCESS)
    {
        Err = CM_MapCrToWin32Err(Cr, ERROR_INVALID_DATA);
        goto cleanup;
    }

    //
    // Register for notifications on existing device interfaces
    //
    for (DeviceInterface = DeviceInterfaceList;
         *DeviceInterface != L'\0';
         DeviceInterface += wcslen(DeviceInterface) + 1)
    {
        RegisterDeviceNotifications(DeviceInterface);
    }

cleanup:

    if (DeviceInterfaceList != NULL)
    {
        free(DeviceInterfaceList);
    }

    return Err;
}


/*++

Routine Description:

    Clean up device interface context

Arguments:

    NULL

Return Value:

    NULL

--*/
VOID
CleanupDeviceInterfaceContext(
    VOID
    )
{
    PDEVICE_CONTEXT Context = NULL;

    //
    // Unregister from the interface first, so that re-appearance of the
    // interface does not cause us to register device events again.
    //
    if (InterfaceNotificationHandle != NULL)
    {
        CM_Unregister_Notification(InterfaceNotificationHandle);
        InterfaceNotificationHandle = NULL;
    }

    //
    // Unregister notifications for device handles.
    //
    while (TRUE)
    {
        AcquireSRWLockExclusive(&DeviceListLock);

        if (IsDeviceListEmpty(&DeviceList))
        {
            ReleaseSRWLockExclusive(&DeviceListLock);
            break;
        }

        //
        // Get next device context
        //
        Context = CONTAINING_RECORD(DeviceList.Flink, DEVICE_CONTEXT, ListEntry);

        //
        // Remove next device from device list
        //
        RemoveDeviceListEntry(DeviceList.Flink);

        ReleaseSRWLockExclusive(&DeviceListLock);

        UnregisterDeviceNotifications(Context);
    }
}

/*++

Routine Description:

    Turns off all of the bar graph lights on the OSR USB FX2 device.

Arguments:

    Context - The callback context

Return Value:

    VOID

--*/
DWORD
ClearAllBars(
    _In_ PDEVICE_CONTEXT Context
    )
{
    DWORD Err = ERROR_SUCCESS;
    ULONG BytesReturned;

    BarGraphState.BarsAsUChar = 0;

    if (!DeviceIoControl(Context->DeviceHandle,
                         IOCTL_OSRUSBFX2_SET_BAR_GRAPH_DISPLAY,
                         &BarGraphState,          // Pointer to InBuffer
                         sizeof(BAR_GRAPH_STATE), // Length of InBuffer
                         NULL,                    // Pointer to OutBuffer
                         0,                       // Length of OutBuffer
                         &BytesReturned,          // BytesReturned
                         0))                      // Pointer to Overlapped structure
    {
        Err = GetLastError();
        WriteToErrorLog(L"DeviceIOControl", Err);
        goto cleanup;
    }

cleanup:

    return Err;
}


/*++

Routine Description:

    Lights the next bar on the OSR USB FX2 device.

Arguments:

    Context - The callback context

Return Value:

    VOID

--*/
DWORD
LightNextBar(
    _In_ PDEVICE_CONTEXT Context
    )
{
    DWORD Err = ERROR_SUCCESS;
    ULONG BytesReturned;

    //
    // Normalize to 0-7
    //
    CurrentBar += 1;

    if (CurrentBar > 7)
    {
        CurrentBar = 0;
    }

    BarGraphState.BarsAsUChar = 1 << (UCHAR)CurrentBar;

    if (!DeviceIoControl(Context->DeviceHandle,
                         IOCTL_OSRUSBFX2_SET_BAR_GRAPH_DISPLAY,
                         &BarGraphState,          // Pointer to InBuffer
                         sizeof(BAR_GRAPH_STATE), // Length of InBuffer
                         NULL,                    // Pointer to OutBuffer
                         0,                       // Length of OutBuffer
                         &BytesReturned,          // BytesReturned
                         0))                      // Pointer to Overlapped structure
    {
        Err = GetLastError();
        WriteToErrorLog(L"DeviceIOControl", Err);
        goto cleanup;
    }

cleanup:

    return Err;
}

/*++

Routine Description:

    Lights the next bar on the OSRFX2 device.

Arguments:

    Context - The device context

Return Value:

    A Win32 error code.

--*/
DWORD
ControlDevice(
    _In_ PDEVICE_CONTEXT Context
    )
{
    DWORD Err = ERROR_SUCCESS;

    Err = ClearAllBars(Context);

    if (Err != ERROR_SUCCESS)
    {
        goto cleanup;
    }

    Err = LightNextBar(Context);

    if (Err != ERROR_SUCCESS)
    {
        goto cleanup;
    }

cleanup:

    return Err;
}
