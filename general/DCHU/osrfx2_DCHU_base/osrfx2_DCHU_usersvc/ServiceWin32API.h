#ifndef _SERVICE_WIN32_API_
#define _SERVICE_WIN32_API_

#include "Main.h"

VOID
WINAPI
ServiceMain(
    DWORD Argc,
    PWSTR *Argv
    );

VOID
WINAPI
ServiceCtrlHandler(
    DWORD Ctrl
    );

BOOL
UpdateServiceStatus(
    __in_opt SERVICE_STATUS_HANDLE hSvcHandle,
    __in     DWORD                 dwCurrentState,
    __in     DWORD                 dwWin32ExitCode
    );

VOID
CALLBACK
ServiceStopCallback(
    _In_ PVOID   lpParameter,
    _In_ BOOLEAN TimerOrWaitFired
    );

VOID
ServiceStop(
    _In_ DWORD ExitCode
    );

DWORD
WINAPI
ServiceRunningWorkerThread(
    _In_ PVOID lpThreadParameter
    );

DWORD
WINAPI
ServiceStopWorkerThread(
    _In_ PVOID lpThreadParameter
    );

#endif // _SERVICE_WIN32_API_
