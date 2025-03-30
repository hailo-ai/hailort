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
    m_last_write_time(std::chrono::steady_clock::now())
{}

void FramerateThrottle::throttle()
{
    if (m_framerate == UNLIMITED_FRAMERATE) {
        return;
    }

    const auto elapsed_time = std::chrono::steady_clock::now() - m_last_write_time;
    std::this_thread::sleep_for(m_framerate_interval - elapsed_time);
    m_last_write_time = std::chrono::steady_clock::now();
}
