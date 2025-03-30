/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hailort_service.cpp
 * @brief main for hailort service 
 * The service code is based on Microsoft's documenataion: https://learn.microsoft.com/en-us/windows/win32/services/the-complete-service-sample
 *
 * Running hailort_service:
 * To run hailort_service without Windows service control manager (SCM), run hailort_service executable with `standalone`.
 *
 * To run as a service application please follow the steps:
 *       1) Compile and install libhailort:
 *          `cmake -H. -Bbuild -A=x64 -DCMAKE_BUILD_TYPE=Release -DHAILO_BUILD_SERVICE=1 && cmake --build build --config release --target install`
 * 
 *       2) To install the service, run the `hailort_service` executable with `install`:
 *          `"C:\Program Files\HailoRT\bin\hailort_service.exe" install`

 *       3) Start the service:
 *           `sc start hailort_service`
 *
 *       4) Stop the service:
 *           `sc stop hailort_service`
 *
 *       5) Delete service:
 *           `sc delete hailort_service`
 */

#include "hailort_rpc_service.hpp"
#include "rpc/rpc_definitions.hpp"
#include "common/os_utils.hpp"
#include "common/os/windows/named_mutex_guard.hpp"

#include <winsvc.h>
#include <windows.h>
#include <tchar.h>
#include <strsafe.h>

using namespace hailort;

#define SERVICE_NAME ("hailort_service")
static const DWORD HRT_SERVICE_INIT_WAIT_TIME_MS(3000);
static const DWORD HRT_SERVICE_ZERO_WAIT_TIME_MS(0);

SERVICE_STATUS g_service_status = {0};
SERVICE_STATUS_HANDLE g_service_status_handle = nullptr;
HANDLE g_stop_event_handle = INVALID_HANDLE_VALUE;
std::unique_ptr<grpc::Server> g_hailort_rpc_server = nullptr;

void RunService()
{
    // Create a named mutex
    auto service_named_mutex = NamedMutexGuard::create(HAILORT_SERVICE_NAMED_MUTEX);
    if (HAILO_SUCCESS != service_named_mutex.status()) {
        LOGGER__ERROR("Failed to create service named mutex with status={}. Please check if another instance is already running.",
            service_named_mutex.status());
        return;
    }

    const std::string server_address = HAILORT_SERVICE_ADDRESS;
    HailoRtRpcService service;
    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.SetMaxReceiveMessageSize(-1);
    builder.RegisterService(&service);
    g_hailort_rpc_server = builder.BuildAndStart();
    g_hailort_rpc_server->Wait();
}

// Installs the service in the SCM database
void install_service()
{
    SC_HANDLE open_sc_manager_handle = nullptr;
    SC_HANDLE create_service_handle = nullptr;
    TCHAR module_path[MAX_PATH];

    if (!GetModuleFileName(nullptr, module_path, MAX_PATH)) {
        HAILORT_OS_LOG_ERROR("GetModuleFileName() failed. Cannot install hailort service, LE = {}", GetLastError());
        return;
    }

    TCHAR quoted_module_path[MAX_PATH];
    StringCbPrintf(quoted_module_path, MAX_PATH, ("\"%s\""), module_path);

    // Get a handle to the SCM database.
    open_sc_manager_handle = OpenSCManager(
        nullptr,                    // local computer
        nullptr,                    // ServicesActive database 
        SC_MANAGER_ALL_ACCESS);     // full access rights 

    if (nullptr == open_sc_manager_handle) {
        HAILORT_OS_LOG_ERROR("OpenSCManager() failed. Cannot install hailort service, LE = {}", GetLastError());
        return;
    }

    // Create the service
    create_service_handle = CreateService(
        open_sc_manager_handle,         // SCM database
        SERVICE_NAME,                   // name of service
        SERVICE_NAME,                   // service name to display
        SERVICE_ALL_ACCESS,             // desired access
        SERVICE_WIN32_OWN_PROCESS,      // service type
        SERVICE_DEMAND_START,           // start type
        SERVICE_ERROR_NORMAL,           // error control type
        quoted_module_path,             // path to service's binary
        nullptr,                        // no load ordering group
        nullptr,                        // no tag identifier
        nullptr,                        // no dependencies
        nullptr,                        // LocalSystem account
        nullptr);                       // no password
 
    if (nullptr == create_service_handle) {
        HAILORT_OS_LOG_ERROR("CreateService() failed. Cannot install hailort service, LE = {}", GetLastError());
        CloseServiceHandle(open_sc_manager_handle);
        return;
    }

    CloseServiceHandle(create_service_handle); 
    CloseServiceHandle(open_sc_manager_handle);
}

// Sets the current service status and reports it to the SCM
void report_service_status(DWORD current_state, DWORD win32_exit_code, DWORD wait_hint)
{
    static DWORD check_point = 1;
    g_service_status.dwCurrentState = current_state;
    g_service_status.dwWin32ExitCode = win32_exit_code;
    g_service_status.dwWaitHint = wait_hint;

    if (SERVICE_START_PENDING == current_state) {
        // Service is about to start
        g_service_status.dwControlsAccepted = 0;
    } else {
        g_service_status.dwControlsAccepted = SERVICE_ACCEPT_STOP;  
    }

    if ((SERVICE_RUNNING == current_state) || (SERVICE_STOPPED == current_state)) {
        g_service_status.dwCheckPoint = 0;
    } else {
        g_service_status.dwCheckPoint = check_point++;
    }

    // Report the service status to the SCM.
    SetServiceStatus(g_service_status_handle, &g_service_status);
}

// Called by SCM whenever a control code is sent to the service
void control_handler(DWORD control_code)
{
    switch(control_code) {
        case SERVICE_CONTROL_STOP:
            report_service_status(SERVICE_STOP_PENDING, NO_ERROR, HRT_SERVICE_ZERO_WAIT_TIME_MS);

            // Signal the service to stop.
            SetEvent(g_stop_event_handle);
            report_service_status(SERVICE_STOPPED, NO_ERROR, HRT_SERVICE_ZERO_WAIT_TIME_MS);
            return;

        default: 
            break;
    }
}

void terminate_server_thread(HANDLE thread_handle)
{
    g_hailort_rpc_server->Shutdown();
    auto rpc_server_wait_res = WaitForSingleObject(thread_handle, INFINITE);
    if (WAIT_OBJECT_0 == rpc_server_wait_res) {
        CloseHandle(thread_handle);
    } else {
        HAILORT_OS_LOG_ERROR("Failed waiting on hailort server thread, LE = {}", GetLastError());
        report_service_status(SERVICE_STOPPED, GetLastError(), HRT_SERVICE_ZERO_WAIT_TIME_MS);
    }
}

// The service code
void service_init()
{
    // Create an event. The control handler function signals this event when it receives the stop control code.
    g_stop_event_handle = CreateEvent(
        nullptr,       // default security attributes
        TRUE,          // manual reset event
        FALSE,         // not signaled
        nullptr);      // no name
    if (nullptr == g_stop_event_handle) {
        report_service_status(SERVICE_STOPPED, GetLastError(), HRT_SERVICE_ZERO_WAIT_TIME_MS);
        return;
    }

    // Report SCM the running status when initialization is complete.
    report_service_status(SERVICE_RUNNING, NO_ERROR, HRT_SERVICE_ZERO_WAIT_TIME_MS);

    // Start a thread that will perform the main task of the service
    HANDLE service_thread_handle = CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)RunService, nullptr, 0, nullptr);
    if (nullptr == service_thread_handle) {
        HAILORT_OS_LOG_ERROR("Failed to create hailort_service thread, LE = {}", GetLastError());
    }

    // Wait for stop service signal
    auto service_wait_res = WaitForSingleObject(g_stop_event_handle, INFINITE);
    if (WAIT_OBJECT_0 == service_wait_res) {
        terminate_server_thread(service_thread_handle);
        report_service_status(SERVICE_STOPPED, NO_ERROR, HRT_SERVICE_ZERO_WAIT_TIME_MS);
    } else {
        HAILORT_OS_LOG_ERROR("Failed waiting for signal on hailort_service stop event, LE = {}", GetLastError());
        report_service_status(SERVICE_STOPPED, GetLastError(), HRT_SERVICE_ZERO_WAIT_TIME_MS);
    }
    CloseHandle(g_stop_event_handle);
}

// Entry point for the service
void service_main(DWORD /* dwArgc */, LPTSTR * /*lpszArgv*/)
{
    // Register the handler function for the service
    g_service_status_handle = RegisterServiceCtrlHandler(SERVICE_NAME, control_handler);
    if (!g_service_status_handle) {
        HAILORT_OS_LOG_ERROR("RegisterServiceCtrlHandler() failed. Cannot start hailort service, LE = {}", GetLastError()); 
        return;
    }

    g_service_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS; 
    g_service_status.dwServiceSpecificExitCode = 0;

    // Report initial status to the SCM - service is starting
    report_service_status(SERVICE_START_PENDING, NO_ERROR, HRT_SERVICE_INIT_WAIT_TIME_MS);
    service_init();
}

int main(int argc, TCHAR *argv[])
{ 
    const bool is_standalone = ((1 < argc) && (0 == lstrcmpi(argv[1], "standalone")));
    if (is_standalone) {
        RunService();
        return 0;
    }

    // If command-line parameter is "install", install the service. 
    // Otherwise, the service is probably being started by the SCM.
    if ((0 < argc) && (0 == lstrcmpi(argv[1], "install"))) {
        install_service();
        return 0;
    }

    // Service is being started by the SCM
    SERVICE_TABLE_ENTRY dispatch_table[] = { 
        {SERVICE_NAME, static_cast<LPSERVICE_MAIN_FUNCTION>(service_main)},
        {nullptr, nullptr}
    };
 
    // This call returns when the service has stopped (SERVICE_STOPPED).
    // The process should simply terminate when the call returns.
    if (!StartServiceCtrlDispatcher(dispatch_table)) {
        HAILORT_OS_LOG_ERROR("StartServiceCtrlDispatcher() failed. Cannot start hailort service, LE = {}", GetLastError());
    }
    return 0;
}