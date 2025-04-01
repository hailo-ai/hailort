/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file barrier.cpp
 **/

#include "common/barrier.hpp"

namespace hailort
{

Barrier::Barrier(size_t count) :
    m_original_count(count), m_count(count), m_generation(0), m_mutex(), m_cv(), m_is_activated(true)
{}

void Barrier::arrive_and_wait()
{
    // Consider adding timeout and returning HAILO_STATUS
    if (!m_is_activated.load()) {
        return;
    }
    std::unique_lock<std::mutex> lock(m_mutex);
    size_t current_generation = m_generation;
    if ((--m_count) == 0) {
        m_generation++;
        m_count = m_original_count;
        m_cv.notify_all();
    }
    else {
        m_cv.wait(lock, [this, current_generation] { return ((current_generation != m_generation) || !m_is_activated); });
    }
}

void Barrier::terminate()
{
    m_is_activated.store(false);
    {
        std::unique_lock<std::mutex> lock(m_mutex);
    }
    m_cv.notify_all();
}

} /* namespace hailort */
