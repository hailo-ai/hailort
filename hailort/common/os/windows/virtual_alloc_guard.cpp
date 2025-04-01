/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file virtual_alloc_guard.cpp
 * @brief Guard object for VirtualAlloc and VirtualFree
 **/

#include "virtual_alloc_guard.hpp"
#include "common/logger_macros.hpp"
#include "common/utils.hpp"

namespace hailort
{

Expected<VirtualAllocGuard> VirtualAllocGuard::create(size_t size)
{
    hailo_status status = HAILO_UNINITIALIZED;
    VirtualAllocGuard guard(size, status);
    CHECK_SUCCESS_AS_EXPECTED(status);
    return guard;
}

VirtualAllocGuard::VirtualAllocGuard(size_t size, hailo_status &status) :
    m_address(VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)),
    m_size(size)
{
    if (nullptr == m_address) {
        status = HAILO_OUT_OF_HOST_MEMORY;
        return;
    }

    status = HAILO_SUCCESS;
}

VirtualAllocGuard::~VirtualAllocGuard()
{
    if (nullptr != m_address) {
        // From msdn - when passing MEM_RELEASE to VirtualFree, 0 must be passed as size.
        static constexpr size_t ZERO_SIZE = 0;
        if (!VirtualFree(m_address, ZERO_SIZE, MEM_RELEASE)) {
            LOGGER__ERROR("VirtualFree failed with error {}", GetLastError());
        }
    }
}

} /* namespace hailort */
