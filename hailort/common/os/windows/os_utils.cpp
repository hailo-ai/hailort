/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file os_utils.cpp
 * @brief Utilities for Windows methods
 **/

#include "common/os_utils.hpp"
#include "common/utils.hpp"
#include "hailo/hailort.h"

#include <windows.h>
#include "spdlog/sinks/win_eventlog_sink.h"

#define CACHE_LEVEL_INDEX (1)

namespace hailort
{

HailoRTOSLogger::HailoRTOSLogger()
{
    auto event_log_sink = std::make_shared<spdlog::sinks::win_eventlog_sink_mt>("hailort_service");
    m_hailort_os_logger = std::make_shared<spdlog::logger>("eventlog", event_log_sink);
    event_log_sink->set_pattern("%v");
    m_hailort_os_logger->set_level(spdlog::level::debug);
}

uint32_t OsUtils::get_curr_pid()
{
    return static_cast<uint32_t>(GetCurrentProcessId());
}

uint32_t OsUtils::get_curr_tid()
{
    return static_cast<uint32_t>(GetCurrentThreadId());
}

bool OsUtils::is_pid_alive(uint32_t pid)
{
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (hProcess == NULL) {
        // Process is not running
        return false;
    }

    DWORD exitCode;
    BOOL result = GetExitCodeProcess(hProcess, &exitCode);

    CloseHandle(hProcess);

    if (result && exitCode == STILL_ACTIVE) {
        return true;
    }
    else {
        return false;
    }
}

void OsUtils::set_current_thread_name(const std::string &name)
{
    (void)name;
}

hailo_status OsUtils::set_current_thread_affinity(uint8_t cpu_index)
{
    const DWORD_PTR affinity_mask = static_cast<DWORD_PTR>(1ULL << cpu_index);
    CHECK(0 != SetThreadAffinityMask(GetCurrentThread(), affinity_mask), HAILO_INTERNAL_FAILURE,
        "SetThreadAffinityMask failed. LE={}", GetLastError());

    return HAILO_SUCCESS;
}

static size_t get_page_size_impl()
{
    SYSTEM_INFO system_info{};
    GetSystemInfo(&system_info);
    return system_info.dwPageSize;
}

size_t OsUtils::get_page_size()
{
    static const auto page_size = get_page_size_impl();
    return page_size;
}

size_t OsUtils::get_dma_able_alignment()
{
    // // Return value if was saved already
    // if (0 != DMA_ABLE_ALIGNMENT) {
    //     return Expected<size_t>(DMA_ABLE_ALIGNMENT);
    // }

    // size_t cacheline_size = 0;
    // DWORD proc_info_struct_size = 0;

    // // We call this function to fail and get the size needed for SYSTEM_LOGICAL_PROCESSOR_INFORMATION struct
    // BOOL ret_val = GetLogicalProcessorInformation(0, &proc_info_struct_size);
    // CHECK_AS_EXPECTED((FALSE == ret_val) && (ERROR_INSUFFICIENT_BUFFER == GetLastError()), HAILO_INTERNAL_FAILURE,
    //     "GetLogicalProcessorInformation Failed with error {}", GetLastError());

    // std::shared_ptr<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> proc_info(
    //     static_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION *>(malloc(proc_info_struct_size)), free);
    // ret_val = GetLogicalProcessorInformation(static_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION *>(proc_info.get()),
    //     &proc_info_struct_size);
    // CHECK_AS_EXPECTED(ret_val, HAILO_INTERNAL_FAILURE, "GetLogicalProcessorInformation Failed with error {}",
    //     GetLastError());

    // for (DWORD i = 0; i < proc_info_struct_size; i++) {
    //     // Assume same cache line for all processors
    //     if ((RelationCache == proc_info.get()[i].Relationship) && (CACHE_LEVEL_INDEX == proc_info.get()[i].Cache.Level)) {
    //         cacheline_size = proc_info.get()[i].Cache.LineSize;
    //         break;
    //     }
    // }

    // // Set static variable to value - so dont need to fetch actual value every function call
    // // TODO HRT-12459: Currently use DMA_ABLE_ALIGNMENT_WRITE_HW_LIMITATION as minimum until after debug - seeing as all
    // // Funtions currently calling this function are for write
    // DMA_ABLE_ALIGNMENT = std::max(HailoRTCommon::DMA_ABLE_ALIGNMENT_WRITE_HW_LIMITATION, static_cast<size_t>(cacheline_size));
    // return Expected<size_t>(DMA_ABLE_ALIGNMENT);
    // TODO: HRT-12495 support page-aligned address on windows
    return get_page_size();
}

CursorAdjustment::CursorAdjustment()
{
    // Enables Vitual Terminal Processing - enables ANSI Escape Sequences on Windows
    // Source: https://stackoverflow.com/questions/52607960/how-can-i-enable-virtual-terminal-processing
    HANDLE h_out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dword_mode = 0;
    GetConsoleMode(h_out, &dword_mode);
    m_previous_output_buffer_mode = dword_mode;
    dword_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(h_out, dword_mode);
}

CursorAdjustment::~CursorAdjustment()
{
        // Return to the original state
        HANDLE h_out = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleMode(h_out, m_previous_output_buffer_mode); // Return the output buffer mode to it's original mode
}
} /* namespace hailort */
