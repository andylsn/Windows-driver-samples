/*++

Copyright (c) Microsoft Corporation.  All rights reserved.

    THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY
    KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR
    PURPOSE.

Module Name:

    ServiceWin32API.cpp

Abstract:

    The file defines the entry point of the application. According to the
    arguments in the command line, the function installs or uninstalls or
    starts the service by calling into different routines.

Environment:

    User mode

--*/

#pragma region Includes
#include <stdio.h>
#include <windows.h>
#include "ServiceWin32API.h"
#pragma endregion

//
// Settings of the service
//
#define SERVICE_NAME             L"OsrUsbFx2UmUserSvc"
#define SERVICE_FLAGS_RUNNING    0x1

//
// Service context
//
SERVICE_STATUS_HANDLE SvcStatusHandle     = NULL;
volatile LONG         SvcControlFlags     = 0;
HANDLE                SvcStoppedEvent     = NULL;
HANDLE                SvcStopRequestEvent = NULL;
HANDLE                SvcStopWaitObject   = NULL;

//
// Device interface context
//
HCMNOTIFICATION   InterfaceNotificationHandle = NULL;
SRWLOCK           DeviceListLock              = SRWLOCK_INIT;
DEVICE_LIST_ENTRY DeviceList;

/*++

Routine Description:

    Entry point for the service.

Arguments:

    Argc - The number of command line arguments

    Argv - The array of command line arguments

Return Value:

    VOID

--*/
INT
wmain(
   INT    Argc,
   WCHAR *Argv[]
   )
{
    SERVICE_TABLE_ENTRY serviceTable[] =
    {
        { SERVICE_NAME, ServiceMain },
        { NULL, NULL }
    };

   return StartServiceCtrlDispatcher(serviceTable);
}

VOID WINAPI ServiceMain(
    DWORD Argc,
    PWSTR *Argv
    )
{
    DWORD Err = ERROR_SUCCESS;

    //
    // Initialize global variables
    //
    InitializeDeviceListHead(&DeviceList);

    while (!IsDebuggerPresent()) {
        Sleep(1000);
    }

    __debugbreak();

    SvcStatusHandle = RegisterServiceCtrlHandler(SERVICE_NAME,
                                                 ServiceCtrlHandler);

    if (SvcStatusHandle == NULL)
    {
        Err = GetLastError();
        goto cleanup;
    }

    UpdateServiceStatus(SvcStatusHandle,
                        SERVICE_START_PENDING,
                        NO_ERROR);

    //
    // Setup device interface context
    //
    Err = SetupDeviceInterfaceContext();

    UpdateServiceStatus(SvcStatusHandle,
                        SERVICE_START_PENDING,
                        NO_ERROR);

    //
    // Set up any variables the service needs.
    //
    SetVariables();

    SvcStoppedEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    if (SvcStoppedEvent == NULL)
    {
        Err = GetLastError();
        goto cleanup;
    }

    SvcStopRequestEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    if (SvcStopRequestEvent == NULL)
    {
        Err = GetLastError();
        goto cleanup;
    }

    //
    // Register callback function for stop event
    //
    if (!RegisterWaitForSingleObject(&SvcStopWaitObject,
                                     SvcStopRequestEvent,
                                     ServiceStopCallback,
                                     NULL,
                                     INFINITE,
                                     WT_EXECUTEONLYONCE | WT_EXECUTEINPERSISTENTTHREAD))
    {
        Err = GetLastError();
        goto cleanup;
    }

    UpdateServiceStatus(SvcStatusHandle,
                        SERVICE_START_PENDING,
                        NO_ERROR);

    //
    // Queue the main service function for execution in a worker thread.
    //
    QueueUserWorkItem(&ServiceRunningWorkerThread,
                      NULL,
                      WT_EXECUTELONGFUNCTION);

    UpdateServiceStatus(SvcStatusHandle,
                        SERVICE_RUNNING,
                        NO_ERROR);

cleanup:

    if (Err != ERROR_SUCCESS)
    {
        ServiceStop(Err);
    }
}

VOID
WINAPI
ServiceCtrlHandler(
    DWORD Ctrl
    )
{
    switch (Ctrl)
    {
    case SERVICE_CONTROL_STOP:
        //
        // Set service stop event
        //
        SetEvent(SvcStopRequestEvent);
        break;
    default:
        break;
    }
}

#define SERVICE_WAIT_HINT_TIME 30000 // 30 seconds

BOOL
UpdateServiceStatus(
    __in_opt SERVICE_STATUS_HANDLE hSvcHandle,
    __in  DWORD  dwCurrentState,
    __in  DWORD  dwWin32ExitCode
    )
{
    SERVICE_STATUS SvcStatus;

    static DWORD dwCheckPoint = 1;

    SvcStatus.dwServiceType             = SERVICE_WIN32_OWN_PROCESS;
    SvcStatus.dwCurrentState            = dwCurrentState;
    SvcStatus.dwWin32ExitCode           = dwWin32ExitCode;
    SvcStatus.dwServiceSpecificExitCode = NO_ERROR;

    if (dwCurrentState == SERVICE_START_PENDING)
    {
        SvcStatus.dwControlsAccepted = 0;
    }
    else
    {
        SvcStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    }

    if ((dwCurrentState == SERVICE_RUNNING) ||
        (dwCurrentState == SERVICE_STOPPED))
    {
        SvcStatus.dwCheckPoint = 0;
        SvcStatus.dwWaitHint = 0;
    }
    else
    {
        SvcStatus.dwCheckPoint = dwCheckPoint++;
        SvcStatus.dwWaitHint = SERVICE_WAIT_HINT_TIME;
    }

    return SetServiceStatus(hSvcHandle, &SvcStatus);
}

DWORD
WINAPI
ServiceRunningWorkerThread(
    _In_ PVOID lpThreadParameter
    )
{
    PDEVICE_CONTEXT    DeviceContext = NULL;
    PDEVICE_LIST_ENTRY Link          = NULL;

    InterlockedOr(&SvcControlFlags, SERVICE_FLAGS_RUNNING);

    //
    // Periodically check if the service is stopping.
    //
    while ((InterlockedOr(&SvcControlFlags, 0) & SERVICE_FLAGS_RUNNING) != 0)
    {
        AcquireSRWLockShared(&DeviceListLock);

        for (Link = DeviceList.Flink; Link != &DeviceList; Link = Link->Flink)
        {
            DeviceContext = CONTAINING_RECORD(Link, DEVICE_CONTEXT, ListEntry);

            ControlDevice(DeviceContext);
        }

        ReleaseSRWLockShared(&DeviceListLock);


        Sleep(2000);  // Simulate some lengthy operations.
    }

    //
    // Signal the stopped event.
    //
    SetEvent(SvcStoppedEvent);

    return 0;
}

VOID
CALLBACK
ServiceStopCallback(
    _In_ PVOID   lpParameter,
    _In_ BOOLEAN TimerOrWaitFired
    )
{
    //
    // Since wait object can not be unregistered in callback function, queue
    // another thread
    //
    QueueUserWorkItem(ServiceStopWorkerThread,
                      lpParameter,
                      WT_EXECUTEDEFAULT);
}

DWORD
WINAPI
ServiceStopWorkerThread(
    _In_ PVOID lpThreadParameter
    )
{
    ServiceStop(NO_ERROR);

    return 0;
}

VOID
ServiceStop(
    _In_ DWORD ExitCode
    )
{
    if (SvcStatusHandle == NULL)
    {
        return;
    }

    UpdateServiceStatus(SvcStatusHandle, SERVICE_STOP_PENDING, ExitCode);

    //
    // Notify the working thread to stop
    //
    if ((InterlockedOr(&SvcControlFlags, 0) & SERVICE_FLAGS_RUNNING) != 0)
    {
        InterlockedAnd(&SvcControlFlags, ~SERVICE_FLAGS_RUNNING);
        WaitForSingleObject(SvcStoppedEvent, INFINITE);
    }

    //
    // Clean up device context after the worker thread has finished.
    //
    CleanupDeviceInterfaceContext();

    //
    // cleanup work
    //
    if (SvcStopWaitObject != NULL)
    {
        UnregisterWait(SvcStopWaitObject);
    }

    if (SvcStopRequestEvent != NULL)
    {
        CloseHandle(SvcStopRequestEvent);
    }

    if (SvcStoppedEvent != NULL)
    {
        CloseHandle(SvcStoppedEvent);
    }

    UpdateServiceStatus(SvcStatusHandle, SERVICE_STOPPED, ExitCode);
}
