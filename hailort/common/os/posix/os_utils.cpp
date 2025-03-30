/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file os_utils.cpp
 * @brief Utilities for Posix methods
 **/

#include "hailo/hailort.h"
#include "common/os_utils.hpp"
#include "common/utils.hpp"
#include "spdlog/sinks/syslog_sink.h"

#include <unistd.h>
#include <signal.h>
#include <sched.h>
#include <sys/syscall.h>

#if defined(__QNX__)
#define OS_UTILS__QNX_PAGE_SIZE (4096)
#endif /* defined(__QNX__) */
namespace hailort
{

#define EXISTENCE_CHECK_SIGNAL (0)

HailoRTOSLogger::HailoRTOSLogger()
{
    m_hailort_os_logger = spdlog::syslog_logger_mt("syslog", "hailort_service", LOG_PID);
    m_hailort_os_logger->set_pattern("%v");
    m_hailort_os_logger->set_level(spdlog::level::debug);
}

uint32_t OsUtils::get_curr_pid()
{
    return getpid();
}

uint32_t OsUtils::get_curr_tid()
{
    return static_cast<uint32_t>(syscall(SYS_gettid));
}

bool OsUtils::is_pid_alive(uint32_t pid)
{
    return (0 == kill(pid, EXISTENCE_CHECK_SIGNAL));
}

void OsUtils::set_current_thread_name(const std::string &name)
{
    (void)name;
#ifndef NDEBUG
    // pthread_setname_np name size is limited to 16 chars (including null terminator)
    assert(name.size() < 16);
    pthread_setname_np(pthread_self(), name.c_str());
#endif /* NDEBUG */
}

hailo_status OsUtils::set_current_thread_affinity(uint8_t cpu_index)
{
#if defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_index, &cpuset);

    static const pid_t CURRENT_THREAD = 0;
    int rc = sched_setaffinity(CURRENT_THREAD, sizeof(cpu_set_t), &cpuset);
    CHECK(rc == 0, HAILO_INTERNAL_FAILURE, "sched_setaffinity failed with status {}", rc);

    return HAILO_SUCCESS;
#elif defined(__QNX__)
    (void)cpu_index;
    // TODO: impl on qnx (HRT-10889)
    return HAILO_NOT_IMPLEMENTED;
#endif
}

size_t OsUtils::get_page_size()
{
    static const auto page_size = sysconf(_SC_PAGESIZE);
    return page_size;
}

size_t OsUtils::get_dma_able_alignment()
{
#if defined(__linux__)
    // TODO: HRT-12494 after supporting in linux, restore this code
    // Return value if was saved already
    // if (0 != DMA_ABLE_ALIGNMENT) {
    //     return Expected<size_t>(DMA_ABLE_ALIGNMENT);
    // }
    // static const auto cacheline_size = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
    // if (-1 == cacheline_size) {
    //     return make_unexpected(HAILO_INTERNAL_FAILURE);
    // }

    // // Set static variable to value - so dont need to fetch actual value every function call
    // // TODO HRT-12459: Currently use DMA_ABLE_ALIGNMENT_WRITE_HW_LIMITATION as minimum until after debug - seeing as all
    // // Funtions currently calling this function are for write
    // DMA_ABLE_ALIGNMENT = std::max(HailoRTCommon::DMA_ABLE_ALIGNMENT_WRITE_HW_LIMITATION, static_cast<size_t>(cacheline_size));
    // return Expected<size_t>(DMA_ABLE_ALIGNMENT);

    return get_page_size();

// TODO: implement on qnx (HRT-12356) - only needed when async api is implemented on qnx
// TODO - URT-13534 - use sys call for QNX OS to get page size
#elif defined(__QNX__)
    return OS_UTILS__QNX_PAGE_SIZE;
#endif
}

CursorAdjustment::CursorAdjustment(){}
CursorAdjustment::~CursorAdjustment(){}

} /* namespace hailort */
