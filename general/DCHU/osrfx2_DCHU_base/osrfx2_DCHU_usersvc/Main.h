/*++

Copyright (c) Microsoft Corporation.  All rights reserved.

    THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY
    KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR
    PURPOSE.

Module Name:

    Main.h

Abstract:

    Implements the functions to control the OSR USB FX2 device.

Environment:

    User mode

--*/

#pragma once

#include <Windows.h>
#include <devioctl.h>
#include <cfgmgr32.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <strsafe.h>
#include <driverspecs.h>
#include <basetyps.h>

#include <initguid.h>

// {573E8C73-0CB4-4471-A1BF-FAB26C31D384}
DEFINE_GUID(GUID_DEVINTERFACE_OSRUSBFX2,
    0x573e8c73, 0xcb4, 0x4471, 0xa1, 0xbf, 0xfa, 0xb2, 0x6c, 0x31, 0xd3, 0x84);

//
// Most device interface paths will fit into a buffer of this size.
// However, some could be longer, and a larger buffer or dynamically
// allocated buffer may be needed for robust code.
//
#define MAX_DEVPATH_LENGTH 1024

#pragma warning(push)
#pragma warning(disable:4201)  // nameless struct/union
#pragma warning(disable:4214)  // bit field types other than int

//
// Device list
//
typedef struct _DEVICE_LIST_ENTRY {
    struct _DEVICE_LIST_ENTRY *Flink;
    struct _DEVICE_LIST_ENTRY *Blink;
} DEVICE_LIST_ENTRY, *PDEVICE_LIST_ENTRY;

FORCEINLINE
VOID
InitializeDeviceListHead(
    _Out_ PDEVICE_LIST_ENTRY Head
    )
{
    Head->Blink = Head->Flink = Head;
}

FORCEINLINE
BOOL
IsDeviceListEmpty(
    _In_ const PDEVICE_LIST_ENTRY ListHead
    )

{
    return (ListHead->Flink == ListHead);
}

FORCEINLINE
VOID
RemoveDeviceListEntry(
    _In_ PDEVICE_LIST_ENTRY Entry
    )
{
    PDEVICE_LIST_ENTRY Prev = Entry->Blink;
    PDEVICE_LIST_ENTRY Next = Entry->Flink;

    Prev->Flink = Next;
    Next->Blink = Prev;
}

FORCEINLINE
VOID
InsertTailDeviceListEntry(
    _Inout_ PDEVICE_LIST_ENTRY Head,
    _Inout_ PDEVICE_LIST_ENTRY Entry
    )
{
    PDEVICE_LIST_ENTRY Tail = Head->Blink;

    Tail->Flink = Entry;
    Entry->Blink = Tail;
    Entry->Flink = Head;
    Head->Blink = Entry;
}

//
// Context for device handle
//
typedef struct _DEVICE_CONTEXT {
    PWSTR             SymbolicLink;
    HANDLE            DeviceHandle;
    HCMNOTIFICATION   DeviceNotificationHandle;
    BOOL              Unregistered;
    DEVICE_LIST_ENTRY ListEntry;
} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

extern HANDLE            SvcStopRequestEvent;
extern HCMNOTIFICATION   InterfaceNotificationHandle;
extern DEVICE_LIST_ENTRY DeviceList;
extern SRWLOCK           DeviceListLock;

//
// Define the structures that will be used by the IOCTL
// interface to the driver
//

//
// BAR_GRAPH_STATE
//
// BAR_GRAPH_STATE is a bit field structure with each
//  bit corresponding to one of the bar graph on the
//  OSRFX2 Development Board
//
#include <pshpack1.h>
typedef struct _BAR_GRAPH_STATE {

    union {

        struct {
            //
            // Individual bars starting from the
            //  top of the stack of bars
            //
            // NOTE: There are actually 10 bars,
            //  but the very top two do not light
            //  and are not counted here
            //
            UCHAR Bar1 : 1;
            UCHAR Bar2 : 1;
            UCHAR Bar3 : 1;
            UCHAR Bar4 : 1;
            UCHAR Bar5 : 1;
            UCHAR Bar6 : 1;
            UCHAR Bar7 : 1;
            UCHAR Bar8 : 1;
        };

        //
        // The state of all the bar graph as a single
        // UCHAR
        //
        UCHAR BarsAsUChar;

    };

}BAR_GRAPH_STATE, *PBAR_GRAPH_STATE;

//
// SWITCH_STATE
//
// SWITCH_STATE is a bit field structure with each
//  bit corresponding to one of the switches on the
//  OSRFX2 Development Board
//
typedef struct _SWITCH_STATE {

    union {
        struct {
            //
            // Individual switches starting from the
            //  left of the set of switches
            //
            UCHAR Switch1 : 1;
            UCHAR Switch2 : 1;
            UCHAR Switch3 : 1;
            UCHAR Switch4 : 1;
            UCHAR Switch5 : 1;
            UCHAR Switch6 : 1;
            UCHAR Switch7 : 1;
            UCHAR Switch8 : 1;
        };

        //
        // The state of all the switches as a single
        // UCHAR
        //
        UCHAR SwitchesAsUChar;

    };


}SWITCH_STATE, *PSWITCH_STATE;

#include <poppack.h>

#pragma warning(pop)

#define IOCTL_INDEX             0x800
#define FILE_DEVICE_OSRUSBFX2   65500U

#define IOCTL_OSRUSBFX2_GET_CONFIG_DESCRIPTOR CTL_CODE(FILE_DEVICE_OSRUSBFX2, \
                                                       IOCTL_INDEX,           \
                                                       METHOD_BUFFERED,       \
                                                       FILE_READ_ACCESS)

#define IOCTL_OSRUSBFX2_RESET_DEVICE  CTL_CODE(FILE_DEVICE_OSRUSBFX2, \
                                               IOCTL_INDEX + 1,       \
                                               METHOD_BUFFERED,       \
                                               FILE_WRITE_ACCESS)

#define IOCTL_OSRUSBFX2_REENUMERATE_DEVICE  CTL_CODE(FILE_DEVICE_OSRUSBFX2, \
                                                     IOCTL_INDEX  + 3,      \
                                                     METHOD_BUFFERED,       \
                                                     FILE_WRITE_ACCESS)

#define IOCTL_OSRUSBFX2_GET_BAR_GRAPH_DISPLAY CTL_CODE(FILE_DEVICE_OSRUSBFX2, \
                                                       IOCTL_INDEX  + 4,      \
                                                       METHOD_BUFFERED,       \
                                                       FILE_READ_ACCESS)


#define IOCTL_OSRUSBFX2_SET_BAR_GRAPH_DISPLAY CTL_CODE(FILE_DEVICE_OSRUSBFX2, \
                                                       IOCTL_INDEX + 5,       \
                                                       METHOD_BUFFERED,       \
                                                       FILE_WRITE_ACCESS)


#define IOCTL_OSRUSBFX2_READ_SWITCHES   CTL_CODE(FILE_DEVICE_OSRUSBFX2, \
                                                 IOCTL_INDEX + 6,       \
                                                 METHOD_BUFFERED,       \
                                                 FILE_READ_ACCESS)


#define IOCTL_OSRUSBFX2_GET_7_SEGMENT_DISPLAY CTL_CODE(FILE_DEVICE_OSRUSBFX2, \
                                                       IOCTL_INDEX + 7,       \
                                                       METHOD_BUFFERED,       \
                                                       FILE_READ_ACCESS)


#define IOCTL_OSRUSBFX2_SET_7_SEGMENT_DISPLAY CTL_CODE(FILE_DEVICE_OSRUSBFX2, \
                                                       IOCTL_INDEX + 8,       \
                                                       METHOD_BUFFERED,       \
                                                       FILE_WRITE_ACCESS)

#define IOCTL_OSRUSBFX2_GET_INTERRUPT_MESSAGE CTL_CODE(FILE_DEVICE_OSRUSBFX2,\
                                                       IOCTL_INDEX + 9,      \
                                                       METHOD_OUT_DIRECT,    \
                                                       FILE_READ_ACCESS)

DWORD
ControlDevice(
    _In_ PDEVICE_CONTEXT Context
    );

VOID
SetVariables(
    VOID
    );

//
// Device interface context
//
DWORD
SetupDeviceInterfaceContext(
    VOID
    );

VOID
CleanupDeviceInterfaceContext(
    VOID
    );

//
// Device notifications related
//
VOID
DeviceQueryRemoveAction(
    _In_ PDEVICE_CONTEXT Context
    );

VOID
DeviceQueryRemoveFailedAction(
    _In_ PDEVICE_CONTEXT Context
    );

VOID
DeviceRemoveCompleteAction(
    _In_ PDEVICE_CONTEXT Context
    );

DWORD
WINAPI
DeviceCallback(
    _In_ HCMNOTIFICATION       hNotify,
    _In_ PVOID                 hContext,
    _In_ CM_NOTIFY_ACTION      Action,
    _In_ PCM_NOTIFY_EVENT_DATA EventData,
    _In_ DWORD                 EventDataSize
    );

DWORD
RegisterDeviceNotifications(
    _In_ PCWSTR DeviceInterfacePath
    );

VOID
UnregisterDeviceNotifications(
    _Inout_ PDEVICE_CONTEXT Context
    );

DWORD
WINAPI
InterfaceCallback(
    _In_ HCMNOTIFICATION       hNotify,
    _In_ PVOID                 hContext,
    _In_ CM_NOTIFY_ACTION      Action,
    _In_ PCM_NOTIFY_EVENT_DATA EventData,
    _In_ DWORD                 EventDataSize
    );

DWORD
RegisterInterfaceNotifications(
    _Out_ PHCMNOTIFICATION pInterfaceNotificationHandle
    );

DWORD
WINAPI
UnregisterDeviceNotificationsWorkerThread(
    _In_ PVOID lpThreadParameter
    );
