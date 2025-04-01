/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file virtual_alloc_guard.hpp
 * @brief Guard object for VirtualAlloc and VirtualFree (only for windows os).
 **/

#ifndef _HAILO_VIRTUAL_ALLOC_GUARD_HPP_
#define _HAILO_VIRTUAL_ALLOC_GUARD_HPP_

#include "hailo/expected.hpp"

#include <utility>

namespace hailort
{

class VirtualAllocGuard final {
public:
    static Expected<VirtualAllocGuard> create(size_t size);
    ~VirtualAllocGuard();

    VirtualAllocGuard(const VirtualAllocGuard &other) = delete;
    VirtualAllocGuard &operator=(const VirtualAllocGuard &other) = delete;
    VirtualAllocGuard(VirtualAllocGuard &&other) :
        m_address(std::exchange(other.m_address, nullptr)),
        m_size(other.m_size)
    {}
    VirtualAllocGuard &operator=(VirtualAllocGuard &&other) = delete;

    void *address() { return m_address; }
    size_t size() const { return m_size; }

private:
    VirtualAllocGuard(size_t size, hailo_status &status);

    void *m_address;
    const size_t m_size;
};

} /* namespace hailort */

#endif /* _HAILO_VIRTUAL_ALLOC_GUARD_HPP_ */
