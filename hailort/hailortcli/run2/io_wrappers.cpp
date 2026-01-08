/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file io_wrappers.cpp
 **/

#include "io_wrappers.hpp"

FramerateThrottle::FramerateThrottle(uint32_t framerate) :
    m_framerate(framerate),
    m_framerate_interval(std::chrono::duration<double>(1) / framerate),
    m_next_time(std::chrono::steady_clock::now())
{}

void FramerateThrottle::throttle()
{
    if (m_framerate == UNLIMITED_FRAMERATE) {
        return;
    }

    if (m_next_time > std::chrono::steady_clock::now()) {
        std::this_thread::sleep_until(m_next_time);
    }
    m_next_time += std::chrono::duration_cast<std::chrono::steady_clock::duration>(m_framerate_interval);
}
