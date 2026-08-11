/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file timeouts.hpp
 * @brief Centralized timeout and retry constants for HailoRT userspace components
 **/

#ifndef _HAILO_TIMEOUTS_HPP_
#define _HAILO_TIMEOUTS_HPP_

#include "hailo/hailort_common.hpp"

#include <chrono>
#include <cstddef>

namespace hailort {

// --- VDMA device ---
constexpr std::chrono::milliseconds VDMA_DEVICE_CONTROL_TIMEOUT(HAILO_EMU_SELECT(1000, 50000));

// --- hRPC ---
constexpr std::chrono::milliseconds HRPC_REQUEST_TIMEOUT(
    HAILO_EMU_SELECT(std::chrono::seconds(10), std::chrono::seconds(5000)));

// --- Server ---
constexpr std::chrono::milliseconds SERVER_WAIT_FOR_VDEVICE_TIMEOUT(
    HAILO_EMU_SELECT(std::chrono::seconds(5), std::chrono::seconds(5000)));
constexpr std::chrono::milliseconds SERVER_KV_CACHE_RELEASE_TIMEOUT(
    HAILO_EMU_SELECT(std::chrono::seconds(5), std::chrono::seconds(5000)));

// --- CLI ---
constexpr uint32_t HAILORTCLI_DEFAULT_VSTREAM_TIMEOUT_MS =
    HAILO_EMU_SELECT(HAILO_DEFAULT_VSTREAM_TIMEOUT_MS, HAILO_DEFAULT_VSTREAM_TIMEOUT_MS * 100);
constexpr std::chrono::milliseconds HAILORTCLI_DEFAULT_TIMEOUT(HAILORTCLI_DEFAULT_VSTREAM_TIMEOUT_MS);

// --- PCIe Tunnel ---
constexpr std::chrono::milliseconds PCIE_TUNNEL_TIMEOUT(
    HAILO_EMU_SELECT(std::chrono::seconds(10), std::chrono::seconds(300)));

// --- Scheduler ---
constexpr std::chrono::milliseconds DEFAULT_SCHEDULER_TIMEOUT(0);

// --- Pipeline ---
constexpr std::chrono::milliseconds BUFFER_POOL_DEFAULT_QUEUE_TIMEOUT(10000);

// --- Infer model ---
constexpr std::chrono::milliseconds WAIT_FOR_ASYNC_IN_DTOR_TIMEOUT(10000);

// --- GenAI / Configure ---
constexpr std::chrono::minutes CCWS_READY_TIMEOUT(2);

} /* namespace hailort */

#endif /* _HAILO_TIMEOUTS_HPP_ */
